/*****************************************************************************\
 *  auth_authd - plugin for Brent Chun's authd
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#  if STDC_HEADERS
#    include <stdio.h>
#    include <string.h>
#  endif /* STDC_HEADERS */
#  if HAVE_UNISTD_H
#    include <unistd.h>
#  endif /* HAVE_UNISTD_H */
#else /* ! HAVE_CONFIG_H */
#  include <stdio.h>
#  include <unistd.h>
#  include <string.h>
#  include <auth.h>
#endif /* HAVE_CONFIG_H */

#include <pwd.h>
#include <grp.h>
#include <auth.h>

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108  /* Cribbed from linux/un.h */
#endif

#include <slurm/slurm_errno.h>
#include "src/common/slurm_xlator.h"

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
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for SLURM authentication) and <method> is a
 * description of how this plugin satisfies that application.  SLURM will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version   - specifies the version number of the plugin.
 * min_plug_version - specifies the minumum version number of incomming
 *                    messages that this plugin can accept
 */
const char plugin_name[]        = "Brent Chun's authd authentication plugin";
const char plugin_type[]        = "auth/authd";
const uint32_t plugin_version   = 100;
const uint32_t min_plug_version = 90;

/*
 * Where to find the timeout in the argument vector.  This is set
 * during initialization and should not change.
 */
static int timeout_idx = -1;

/* Default timeout. */
static const int AUTHD_TTL = 2;

typedef struct _slurm_auth_credential {
	credentials cred;
	signature sig;
	int cr_errno;
} slurm_auth_credential_t;

/* Plugin-global errno. */
static int plugin_errno = SLURM_SUCCESS;

/*
 * Errno values.  If you add something here you have to add its
 * corresponding error string in slurm_auth_errstr() below.
 */
enum {
	SLURM_AUTH_UNPACK = SLURM_AUTH_FIRST_LOCAL_ERROR,
	SLURM_AUTH_EXPIRED
};


int init( void )
{
	const arg_desc_t *desc;

	verbose( "authd authentication module initializing" );

	if ( ( desc = slurm_auth_get_arg_desc() ) == NULL ) {
		error( "unable to query SLURM for argument vector layout" );
		return SLURM_ERROR;
	}

	if ( ( timeout_idx = arg_idx_by_name( desc, ARG_TIMEOUT ) ) < 0 ) {
		error( "Required argument 'Timeout' not provided" );
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
}

slurm_auth_credential_t *
slurm_auth_create( void *argv[], char *auth_info )
{
	int ttl;
	int rc;
	slurm_auth_credential_t *cred;

	if ( argv == NULL ) {
		plugin_errno = SLURM_AUTH_MEMORY;
		return NULL;
	}

	cred = (slurm_auth_credential_t *)
		xmalloc( sizeof( slurm_auth_credential_t ) );
	cred->cr_errno = SLURM_SUCCESS;
	cred->cred.uid = geteuid();
	cred->cred.gid = getegid();

	cred->cred.valid_from = time( NULL );

	ttl = timeout_idx >= 0 ? (int) argv[ timeout_idx ] : AUTHD_TTL;
	/*
	 * In debug mode read the time-to-live from an environment
	 * variable.
	 */
#ifndef NDEBUG
	{
		char *env = getenv( "SLURM_AUTHD_TTL" );
		if ( env ) {
			ttl = atoi( env );
			if ( ttl <= 0 ) ttl = AUTHD_TTL;
		}
	}
#endif /*NDEBUG*/

	cred->cred.valid_to = cred->cred.valid_from + ttl;

	/* Sign the credential. */
	auth_init_credentials (&cred->cred, ttl);
	if ((rc = auth_get_signature( &cred->cred, &cred->sig )) < 0 ) {
		plugin_errno = SLURM_AUTH_INVALID;
		xfree( cred );
		return NULL;
	}

	return cred;
}

int
slurm_auth_destroy( slurm_auth_credential_t *cred )
{
	if ( cred == NULL ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}
	xfree( cred );
	return SLURM_SUCCESS;
}

int
slurm_auth_verify( slurm_auth_credential_t *cred, char *auth_info )
{
	int rc;
	time_t now;

	if ( cred == NULL ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}

	if ((rc = auth_verify_signature( &cred->cred, &cred->sig )) < 0) {
		cred->cr_errno = SLURM_AUTH_INVALID;
		return SLURM_ERROR;
	}

	now = time( NULL );
	if ( ( now < cred->cred.valid_from ) || ( now > cred->cred.valid_to ) ) {
		cred->cr_errno = SLURM_AUTH_EXPIRED;
		return SLURM_ERROR;
	}

	/* XXX check to see if user is valid on the system. */

	return SLURM_SUCCESS;
}


uid_t
slurm_auth_get_uid( slurm_auth_credential_t *cred, char *auth_info )
{
	if ( cred == NULL ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_AUTH_NOBODY;
	}
	return cred->cred.uid;
}


gid_t
slurm_auth_get_gid( slurm_auth_credential_t *cred, char *auth_info )
{
	if ( cred == NULL ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_AUTH_NOBODY;
	}
	return cred->cred.gid;
}


int
slurm_auth_pack( slurm_auth_credential_t *cred, Buf buf )
{
	uint16_t sig_size = sizeof( signature );

	if ( ( cred == NULL ) || ( buf == NULL ) ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}

	/*
	 * Marshall the plugin type and version for runtime sanity check.
	 * Add the terminating zero so we get it for free at the
	 * other end.
	 */
	packmem( (char *) plugin_type, strlen( plugin_type ) + 1, buf );
	pack32( plugin_version, buf );

	pack32( (uint32_t) cred->cred.uid, buf );
	pack32( (uint32_t) cred->cred.gid, buf );
	pack_time( cred->cred.valid_from, buf );
	pack_time( cred->cred.valid_to, buf );
	packmem( cred->sig.data, sig_size, buf );

	return SLURM_SUCCESS;
}


slurm_auth_credential_t *
slurm_auth_unpack( Buf buf )
{
	slurm_auth_credential_t *cred = NULL;
	uint16_t sig_size; /* ignored */
	uint32_t version, tmpint;
	char *data;

	if ( buf == NULL ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return NULL;
	}


	/* Check the plugin type. */
	safe_unpackmem_ptr( &data, &sig_size, buf );
	if ( strcmp( data, plugin_type ) != 0 ) {
		plugin_errno = SLURM_AUTH_MISMATCH;
		return NULL;
	}

	safe_unpack32( &version, buf );
	if( version < min_plug_version ) {
		plugin_errno = SLURM_AUTH_VERSION;
		return NULL;
	}

	/* Allocate a credential. */
	cred = (slurm_auth_credential_t *)
		xmalloc( sizeof( slurm_auth_credential_t ) );
	cred->cr_errno = SLURM_SUCCESS;

	safe_unpack32( &tmpint, buf );
	cred->cred.uid = tmpint;
	safe_unpack32( &tmpint, buf );
	cred->cred.gid = tmpint;
	safe_unpack_time( &cred->cred.valid_from, buf );
	safe_unpack_time( &cred->cred.valid_to, buf );
	safe_unpackmem_ptr( &data, &sig_size, buf );
	memcpy( cred->sig.data, data, sizeof( signature ) );

	return cred;

 unpack_error:
	plugin_errno = SLURM_AUTH_UNPACK;
	xfree( cred );
	return NULL;
}


int
slurm_auth_print( slurm_auth_credential_t *cred, FILE *fp )
{
	if ( cred == NULL ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}

	verbose( "BEGIN AUTHD CREDENTIAL\n" );
	verbose( "   UID: %u", cred->cred.uid );
	verbose( "   GID: %u", cred->cred.gid );
	verbose( "   Valid from: %s", ctime( &cred->cred.valid_from ) );
	verbose( "   Valid to: %s", ctime( &cred->cred.valid_to ) );
	verbose( "   Signature: 0x%02x%02x%02x%02x ...\n",
			 cred->sig.data[ 0 ], cred->sig.data[ 1 ],
			 cred->sig.data[ 2 ], cred->sig.data[ 3 ] );
	verbose( "END AUTHD CREDENTIAL\n" );

	return SLURM_SUCCESS;
}


int
slurm_auth_errno( slurm_auth_credential_t *cred )
{
	if ( cred == NULL )
		return plugin_errno;
	else
		return cred->cr_errno;
}


const char *
slurm_auth_errstr( int slurm_errno )
{
	static struct {
		int err;
		char *msg;
	} tbl[] = {
		{ SLURM_AUTH_UNPACK, "cannot unpack authentication type" },
		{ SLURM_AUTH_EXPIRED, "the credential has expired" },
		{ 0, NULL }
	};

	int i;

	for ( i = 0; ; ++i ) {
		if ( tbl[ i ].msg == NULL ) return "unknown error";
		if ( tbl[ i ].err == slurm_errno ) return tbl[ i ].msg;
	}
}
