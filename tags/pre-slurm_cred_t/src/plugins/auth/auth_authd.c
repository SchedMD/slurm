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
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/pack.h"
#include "src/common/log.h"

const char plugin_name[]	= "Brett Chun's authd authentication plugin";
const char plugin_type[]	= "auth/authd";
const uint32_t plugin_version = 90;

typedef struct _slurm_auth_credential {
	credentials cred;
	signature sig;
} slurm_auth_credential_t;

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

slurm_auth_credential_t *
slurm_auth_alloc( void )
{
	return (slurm_auth_credential_t *) xmalloc( sizeof( slurm_auth_credential_t ) );
}

void
slurm_auth_free( slurm_auth_credential_t *cred )
{
	if ( cred != NULL ) xfree( cred );
}

int
slurm_auth_activate( slurm_auth_credential_t *cred, int ttl )
{
	if ( cred == NULL ) return SLURM_ERROR;
	if ( ttl < 1 ) return SLURM_ERROR;
	
	/* Initialize credential with our user and group. */
	cred->cred.uid = geteuid();
	cred->cred.gid = getegid();

	/* Set a valid time interval. */
	cred->cred.valid_from = time( NULL );
	cred->cred.valid_to = cred->cred.valid_from + ttl;

	/* Sign the credential. */
	if ( slurm_auth_get_signature( &cred->cred, &cred->sig ) < 0 ) {
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

int
slurm_auth_verify( slurm_auth_credential_t *cred )
{
	int rc;

	if ( cred == NULL ) return SLURM_ERROR;
	
	rc = slurm_auth_verify_signature( &cred->cred, &cred->sig );
	if ( rc < 0 ) {
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}


uid_t
slurm_auth_get_uid( slurm_auth_credential_t *cred )
{
	xassert( cred );
	return cred->cred.uid;
}


gid_t
slurm_auth_get_gid( slurm_auth_credential_t *cred )
{
	xassert( cred );
	return cred->cred.gid;
}


void
slurm_auth_pack( slurm_auth_credential_t *cred, Buf buf )
{
	uint16_t sig_size = sizeof( signature );

	if ( ( cred == NULL ) || ( buf == NULL ) ) return;

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
}


int
slurm_auth_unpack( slurm_auth_credential_t *cred, Buf buf )
{
	uint16_t sig_size; /* ignored */
	uint32_t version;	
	char *data;

	if ( ( cred == NULL) || ( buf == NULL ) )
		return SLURM_ERROR;

	/* Check the plugin type. */
	unpackmem_ptr( &data, &sig_size, buf );
	if ( strcmp( data, plugin_type ) != 0 ) {
		error( "authd plugin: authentication mismatch, got %s", data );
		return SLURM_ERROR;
	}
	unpack32( &version, buf );
	
	unpack32( &cred->cred.uid, buf );
	unpack32( &cred->cred.gid, buf );
	unpack_time( &cred->cred.valid_from, buf );
	unpack_time( &cred->cred.valid_to, buf );
	unpackmem_ptr( &data, &sig_size, buf );
	memcpy( cred->sig.data, data, sizeof( signature ) );

	return SLURM_SUCCESS;
}


void
slurm_auth_print( slurm_auth_credential_t *cred, FILE *fp )
{
	if ( cred == NULL ) return;

	verbose( "BEGIN AUTHD CREDENTIAL\n" );
	verbose( "   UID: %d", cred->cred.uid );
	verbose( "   GID: %d", cred->cred.gid );
	verbose( "   Valid from: %s", ctime( &cred->cred.valid_from ) );
	verbose( "   Valid to: %s", ctime( &cred->cred.valid_to ) );
	verbose( "   Signature: 0x%02x%02x%02x%02x ...\n",
			 cred->sig.data[ 0 ], cred->sig.data[ 1 ],
			 cred->sig.data[ 2 ], cred->sig.data[ 3 ] );
	verbose( "END AUTHD CREDENTIAL\n" );
}

