/*****************************************************************************\
 *  auth_authd - plugin for Brent Chun's authd
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
 *  UCRL-CODE-2002-040.
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
#  if HAVE_SSL
#    include <openssl/rsa.h>
#    include <openssl/pem.h>
#    include <openssl/err.h>
#  endif /* HAVE_SSL*/
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
#  include <openssl/rsa.h>
#  include <openssl/pem.h>
#  include <openssl/err.h>
#  include <auth.h>
#endif /* HAVE_CONFIG_H */

#include <pwd.h>
#include <grp.h>
#include <auth.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>

/* Need these regardless of how main SLURM transport is abstracted. */
#include <sys/socket.h>
#include <sys/un.h>

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108  /* Cribbed from linux/un.h */
#endif

#include <slurm/slurm_errno.h>
#include "src/common/slurm_auth.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/pack.h"
#include "src/common/log.h"
#include "src/common/arg_desc.h"

const char plugin_name[]	= "Brett Chun's authd authentication plugin";
const char plugin_type[]	= "auth/authd";
const uint32_t plugin_version = 90;

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

/*
 * These come from /usr/include/auth.h which should be installed
 * as part of the authd installation.
 */
static char *cli_path = AUTH_SOCK_PATH;
static char *svr_path = AUTHD_SOCK_PATH;
static char *pub_key_file = AUTH_PUB_KEY;

/*
 * Write bytes reliably to a file descriptor.
 */
static int
write_bytes( int fd, char *buf, size_t size )
{
	ssize_t bytes_remaining, bytes_written;
	char *ptr;

	bytes_remaining = size;
	ptr = buf;
	while ( bytes_remaining > 0 ) {
		bytes_written = write( fd, ptr, size );
		if ( bytes_written < 0 ) return -1;
		bytes_remaining -= bytes_written;
		ptr += bytes_written;
	}
	return 0;
}

/*
 * Read bytes reliably from a file descriptor.
 */
static int
read_bytes( int fd, char *buf, size_t size )
{
	ssize_t bytes_remaining, bytes_read;
	char *ptr;

	bytes_remaining = size;
	ptr = buf;
	while ( bytes_remaining > 0 ) {
		bytes_read = read( fd, ptr, size );
		if ( bytes_read < 0 ) return -1;
		bytes_remaining -= bytes_read;
		ptr += bytes_read;
	}
	return 0;
}

/*
 * These two cribbed from auth.c in the authd distribution.  They would
 * normally be available in the authd library, but the library relies on
 * Brett Chun's enormous and irrelevant convenience library, and we only
 * need to make one call to that library here.  So we inline the code
 * from his library and sever the dependency.
 */
static int
slurm_auth_get_signature( credentials *cred, signature *sig )
{
	int sock;
	char cli_name[ UNIX_PATH_MAX ];
	struct sockaddr_un cli_addr;
	struct sockaddr_un svr_addr;
	socklen_t addr_len = sizeof( struct sockaddr_un );

	if ( ( sock = socket( AF_UNIX, SOCK_STREAM, 0 ) ) < 0 ) {
		return -1;
	}

	cli_addr.sun_family = AF_UNIX;	
	memset( cli_addr.sun_path, 0, UNIX_PATH_MAX );
	sprintf( cli_name, cli_path, getpid() );
	strcpy( &cli_addr.sun_path[ 1 ], cli_name );
	if ( bind( sock, (struct sockaddr *) &cli_addr, addr_len ) < 0 ) {
		error( "authd plugin: cannot bind socket to authd" );
		close( sock );
		return -1;
	}

	svr_addr.sun_family = AF_UNIX;
	memset( svr_addr.sun_path, 0, UNIX_PATH_MAX );
	strcpy( &svr_addr.sun_path[ 1 ], svr_path );
	if ( connect( sock, (struct sockaddr *) &svr_addr, addr_len ) < 0 ) {
		error( "suthd plugin: cannot connect to authd" );
		close( sock );
		return -1;
	}
	
	if ( write_bytes( sock, (char *) cred, sizeof( credentials ) ) < 0 ) {
		error( "authd plugin: cannot write to authd" );
		close( sock );
		return -1;
	}
	if ( read_bytes( sock, (char *) sig, sizeof( signature ) ) < 0 ) {
		error( "authd plugin: cannot read from authd" );
		close( sock );
		return -1;
	}
	close( sock );
	return 0;	
}

static int
slurm_auth_verify_signature( credentials *cred, signature *sig )
{
	int rc_error = 0;
	RSA *pub_key = NULL;
	FILE *f = NULL;

	if ( ( f = fopen( pub_key_file, "r" ) ) == NULL ) {		
		rc_error = -1;
		error( "authd plugin: cannot open public key file %s", pub_key_file );
		goto cleanup;
	}

	if ( ( pub_key = PEM_read_RSA_PUBKEY( f, NULL, NULL, NULL ) ) == NULL ) {
		error( "authd plugin: cannot read RSA public key" );
		rc_error = -1;
		goto cleanup;
	}

	ERR_load_crypto_strings();
	if ( RSA_verify( 0,
					 (unsigned char *) cred, sizeof( credentials ),
					 sig->data, AUTH_RSA_SIGLEN,
					 pub_key ) == 0 ) {
		rc_error = -1;
		error( "authd plugin: cannot verify signature" );
		goto cleanup;
	}

 cleanup:
	if ( pub_key != NULL ) RSA_free( pub_key );
	if ( f != NULL ) fclose( f );
	return rc_error;
}


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


slurm_auth_credential_t *
slurm_auth_create( void *argv[] )
{
	int ttl;	
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
	if ( slurm_auth_get_signature( &cred->cred, &cred->sig ) < 0 ) {
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
slurm_auth_verify( slurm_auth_credential_t *cred, void *argv[] )
{
	int rc;
	time_t now;

	if ( ( cred == NULL ) || ( argv == NULL ) ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}
	
	rc = slurm_auth_verify_signature( &cred->cred, &cred->sig );
	if ( rc < 0 ) {
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
slurm_auth_get_uid( slurm_auth_credential_t *cred )
{
	if ( cred == NULL ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_AUTH_NOBODY;
	}
	return cred->cred.uid;
}


gid_t
slurm_auth_get_gid( slurm_auth_credential_t *cred )
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
	
	pack32( cred->cred.uid, buf );
	pack32( cred->cred.gid, buf );
	pack_time( cred->cred.valid_from, buf );
	pack_time( cred->cred.valid_to, buf );
	packmem( cred->sig.data, sig_size, buf );

	return SLURM_SUCCESS;
}


slurm_auth_credential_t *
slurm_auth_unpack( Buf buf )
{
	slurm_auth_credential_t *cred;
	uint16_t sig_size; /* ignored */
	uint32_t version;	
	char *data;

	if ( buf == NULL ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return NULL;
	}

	
	/* Check the plugin type. */
	if ( unpackmem_ptr( &data, &sig_size, buf ) != SLURM_SUCCESS ) {
		plugin_errno = SLURM_AUTH_UNPACK;
		return NULL;
	}
	if ( strcmp( data, plugin_type ) != 0 ) {
		plugin_errno = SLURM_AUTH_MISMATCH;
		return NULL;
	}

	if ( unpack32( &version, buf ) != SLURM_SUCCESS ) {
		plugin_errno = SLURM_AUTH_UNPACK;
		return NULL;
	}

	/* Allocate a credential. */
	cred = (slurm_auth_credential_t *)
		xmalloc( sizeof( slurm_auth_credential_t ) );
	cred->cr_errno = SLURM_SUCCESS;

	if ( unpack32( &cred->cred.uid, buf ) != SLURM_SUCCESS ) {
		plugin_errno = SLURM_AUTH_UNPACK;
		goto unpack_error;
	}
	if ( unpack32( &cred->cred.gid, buf ) != SLURM_SUCCESS ) {
		plugin_errno = SLURM_AUTH_UNPACK;
		goto unpack_error;
	}
	if ( unpack_time( &cred->cred.valid_from, buf ) != SLURM_SUCCESS ) {
		plugin_errno = SLURM_AUTH_UNPACK;
		goto unpack_error;
	}
	if ( unpack_time( &cred->cred.valid_to, buf ) != SLURM_SUCCESS ) {
		plugin_errno = SLURM_AUTH_UNPACK;
		goto unpack_error;
	}
	if ( unpackmem_ptr( &data, &sig_size, buf ) != SLURM_SUCCESS ) {
		plugin_errno = SLURM_AUTH_UNPACK;
		goto unpack_error;
	}
	memcpy( cred->sig.data, data, sizeof( signature ) );

	return cred;

 unpack_error:
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
	verbose( "   UID: %d", cred->cred.uid );
	verbose( "   GID: %d", cred->cred.gid );
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
