/*****************************************************************************\
 *  auth_krb5.c - Kerberos version 5 slurm authentication plugin, validates 
 *  all RPCs.
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, Alan Riddle <riddle1@llnl.gov>
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#  if STDC_HEADERS
#    include <string.h>
#  endif
#  if HAVE_SYS_TYPES_H
#    include <sys/types.h>
#  endif /* HAVE_SYS_TYPES_H */
#  if HAVE_UNISTD_H
#    include <unistd.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else /* ! HAVE_INTTYPES_H */
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif /* HAVE_INTTYPES_H */
#else /* ! HAVE_CONFIG_H */
#  include <sys/types.h>
#  include <unistd.h>
#  include <stdint.h>
#  include <string.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>

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
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the authentication API matures.
 */
const char plugin_name[]       	= "Kerberos v5 authentication plugin";
const char plugin_type[]       	= "auth/krb5";
const uint32_t plugin_version	= 90;


/*
 * An opaque type representing authentication credentials.  This type can be
 * called whatever is meaningful and may contain any data required by the
 * plugin.  However, the plugin must be able to recover the Linux UID and
 * GID given only an object of this type.
 *
 * The client code never sees the inside of this structure directly.
 * Objects of this type are passed in and out of the plugin via
 * anonymous pointers.  Because of this, robust plugins may wish to add
 * some form of runtime typing to ensure that the pointers they have
 * received are actually appropriate credentials and not pointers to
 * random memory.
 *
 * A word about thread safety.  The authentication plugin API specifies
 * that SLURM will exercise the plugin sanely.  That is, the authenticity
 * of a credential which has not been activated should not be tested.
 * However, the credential should be thread-safe.  This does not mean
 * necessarily that a plugin must recognize when an inconsistent sequence
 * of API calls is in progress, but if a plugin will crash or otherwise
 * misbehave if it is handed a credential in an inconsistent state (perhaps
 * it is in the process of being activated and a signature is incomplete)
 * then it is the plugin's responsibility to provide its own serialization
 * to avoid that.
 *
 */
typedef struct _slurm_auth_credential {
	/* Kerberos ticket goes here */
	uid_t uid;		/* Generated after ticket is decoded and 
				 * principla is mapped to UID */
	gid_t gid;		/* Generated after cred is decoded */
	int cr_errno;		/* Error code, if applicable */
} slurm_auth_credential_t;

/* A plugin-global errno. */
static int plugin_errno = SLURM_SUCCESS;

/*
 * New errno values particular to this plugin.  We declare the first
 * one to be SLURM_AUTH_FIRST_LOCAL_ERROR to avoid conflicting with
 * the general ones.
 */
enum {
	SLURM_AUTH_UNPACK_TYPE = SLURM_AUTH_FIRST_LOCAL_ERROR,
	SLURM_AUTH_UNPACK_VERSION,
	SLURM_AUTH_UNPACK_CRED,
	SLURM_AUTH_INVALID_PRINCIPAL,
	SLURM_AUTH_CRED_INVALID,
	SLURM_AUTH_CRED_EXPIRED
};

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is unloaded. It should terminate
 * pthreads, free memory, etc.
 */
extern int fini ( void )
{
	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard SLURM authentication
 * API.
 */

/*
 * Allocate and initializes a credential.  This function should return
 * NULL if it cannot allocate a credential.
 */
slurm_auth_credential_t *
slurm_auth_create( void *argv[] )
{
	slurm_auth_credential_t *cred;

	cred = ((slurm_auth_credential_t *) 
		xmalloc( sizeof( slurm_auth_credential_t ) ));
	cred->cr_errno = SLURM_SUCCESS;
	cred->uid = geteuid();
	cred->gid = getegid();
	return cred;
}

/*
 * Free a credential that was allocated with slurm_auth_create() or
 * slurm_auth_unpack().
 */
int
slurm_auth_destroy( slurm_auth_credential_t *cred )
{
	if ( cred == NULL ) {
		plugin_errno = SLURM_AUTH_MEMORY;
		return SLURM_ERROR;
	}
	xfree( cred );
	return SLURM_SUCCESS;
}

/*
 * Verify a credential to approve or deny authentication.
 *
 * Return SLURM_SUCCESS if the credential is in order and valid.
 */
int
slurm_auth_verify( slurm_auth_credential_t *cred, void *argv[] )
{
#if 0	/* FIXME */
	/* validate the kerberos ticket and 
	 * use the kerberos principal name to get the uid and gid */
	struct passwd *pw = getpwnam(cred->principal);
	if (!pw) {
		error("Could not find UID for principal %s",
			cred->principal);
		cred->cr_errno = SLURM_AUTH_INVALID_PRINCIPAL;
		plugin_errno   = SLURM_AUTH_INVALID_PRINCIPAL;
		return SLURM_ERROR;
	}
	cred->uid = pw->pw_uid;
	cred->gid = pw->pw_gid;
#endif
	return SLURM_SUCCESS;
}

/*
 * Obtain the Linux UID from the credential.  The accuracy of this data
 * is not assured until slurm_auth_verify() has been called for it.
 */
uid_t
slurm_auth_get_uid( slurm_auth_credential_t *cred )
{
	if ( cred == NULL ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_AUTH_NOBODY;
	} else {
		return cred->uid;
	}
}

/*
 * Obtain the Linux GID from the credential.  The accuracy of this data
 * is not assured until slurm_auth_verify() has been called for it.
 */
gid_t
slurm_auth_get_gid( slurm_auth_credential_t *cred )
{
	if ( cred == NULL ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_AUTH_NOBODY;
	} else {
		return cred->gid;
	}
}

/*
 * Marshall a credential for transmission over the network, according to
 * SLURM's marshalling protocol.
 */
int
slurm_auth_pack( slurm_auth_credential_t *cred, Buf buf )
{
	if ( ( cred == NULL ) || ( buf == NULL ) ) {
		plugin_errno = SLURM_AUTH_BADARG;
		if (cred)
			cred->cr_errno = plugin_errno;
		return SLURM_ERROR;
	}
	
	/*
	 * Prefix the credential with a description of the credential
	 * type so that it can be sanity-checked at the receiving end.
	 */
	packmem( (char *) plugin_type, strlen( plugin_type ) + 1, buf );
	pack32( plugin_version, buf );
	/*
	 * Pack the data values.
	 */
	pack32( (uint32_t) cred->uid, buf );
	pack32( (uint32_t) cred->gid, buf );

	return SLURM_SUCCESS;
}

/*
 * Unmarshall a credential after transmission over the network according
 * to SLURM's marshalling protocol.
 */
slurm_auth_credential_t *
slurm_auth_unpack( Buf buf )
{
	slurm_auth_credential_t *cred;
	char *tmpstr;
	uint32_t tmpint;
	uint32_t version;
	uint16_t size;
	
	if ( buf == NULL ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return NULL;
	}

	/*
	 * Get the authentication type.
	 */
	if ( unpackmem_ptr( &tmpstr, &size, buf ) != SLURM_SUCCESS ) {
		plugin_errno = SLURM_AUTH_UNPACK_TYPE;
		return NULL;
	}
	if (( tmpstr == NULL )
	||  ( strcmp( tmpstr, plugin_type ) != 0 )) {
		plugin_errno = SLURM_AUTH_MISMATCH;
		return NULL;
	}
	if ( unpack32( &version, buf ) != SLURM_SUCCESS ) {
		plugin_errno = SLURM_AUTH_UNPACK_VERSION;
		return NULL;
	}
	if ( version != plugin_version ) {
		plugin_errno = SLURM_AUTH_MISMATCH;
		return NULL;
	}

	/* Allocate a new credential. */
	cred = ((slurm_auth_credential_t *)
		xmalloc( sizeof( slurm_auth_credential_t ) ));
	cred->cr_errno = SLURM_SUCCESS;

	/*
	 * We do it the hard way because we don't know anything about the
	 * size of uid_t or gid_t, only that they are integer values.  We
	 * pack them as 32-bit integers, but we can't pass addresses to them
	 * directly to unpack as 32-bit integers because there will be bad
	 * clobbering if they really aren't.  This technique ensures a
	 * warning at compile time if the sizes are incompatible.
	 */
	if ( unpack32( &tmpint, buf ) != SLURM_SUCCESS ) {
		plugin_errno = SLURM_AUTH_UNPACK_CRED;
		xfree( cred );
		return NULL;
	}
	cred->uid = tmpint;
	if ( unpack32( &tmpint, buf ) != SLURM_SUCCESS ) {
		plugin_errno = SLURM_AUTH_UNPACK_CRED;
		xfree( cred );
		return NULL;
	}
	cred->gid = tmpint;

	return cred;
}

/*
 * Print to a stdio stream a human-readable representation of the
 * credential for debugging or logging purposes.  The format is left
 * to the imagination of the plugin developer.
 */
int
slurm_auth_print( slurm_auth_credential_t *cred, FILE *fp )
{
	if ( ( cred == NULL) || ( fp == NULL ) ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}

	printf( "BEGIN SLURM BASIC AUTHENTICATION CREDENTIAL\n" );
#if 0	/* FIXME */
	printf( "\tSTARTING  = %s\n",
	printf( "\tENDING    = %s\n",
	printf( "\tRENEW TO  = %s\n",
	printf( "\tPRINCIPAL = %s\n",
	printf( "\tFLAGS     = %s\n",
#endif
	printf( "\tUID       = %u\n", cred->uid );
	printf( "\tGID       = %u\n", cred->gid );
	printf( "END SLURM BASIC AUTHENTICATION CREDENTIAL\n" );

	return SLURM_SUCCESS;
}


/*
 * Return the errno.  If no credential is given, return the errno
 * of the plugin.  This leads to possibly ambiguous situations, but
 * there really isn't any easy way of dealing with that.
 */
int
slurm_auth_errno( slurm_auth_credential_t *cred )
{
	if ( cred == NULL )
		return plugin_errno;
	else
		return cred->cr_errno;
}


/*
 * Return a string corresponding to an error.  We are responsible only for
 * the errors we define here in the plugin.  The SLURM plugin wrappers
 * take care of the API-mandated errors.
 */
const char *
slurm_auth_errstr( int slurm_errno )
{
	static struct {
		int err;
		char *msg;
	} tbl[] = {
		{ SLURM_AUTH_UNPACK_TYPE, "cannot unpack authentication type" },
		{ SLURM_AUTH_UNPACK_VERSION, "cannot unpack credential version" },
		{ SLURM_AUTH_UNPACK_CRED, "cannot unpack credential" },
		{ SLURM_AUTH_INVALID_PRINCIPAL, "invalid principal/user" },
		{ SLURM_AUTH_CRED_INVALID, "credential is invalid" },
		{ SLURM_AUTH_CRED_EXPIRED, "credential has expired" },
		{ 0, NULL }
	};

	int i;

	for ( i = 0; ; ++i ) {
		if ( tbl[ i ].msg == NULL ) return "unknown error";
		if ( tbl[ i ].err == slurm_errno ) return tbl[ i ].msg;
	}
}
