/*****************************************************************************\
 * slurm_auth_imple.h - authentication implementation for Brent Chun's authd
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by AUTHOR <AUTHOR@llnl.gov>.
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
#include <config.h>
#endif

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>

/*
 * Because authd's authentication scheme relies on determining the
 * owner of a Unix domain socket over which the request is made to
 * authenticate credentials, we must do our own socket thing unless
 * the transport abstraction layer will provide for Unix domain
 * sockets.
 */
#include <sys/socket.h>
#include <sys/un.h>

#include <src/common/slurm_authentication.h>
#include <src/common/slurm_errno.h>
#include <src/common/xmalloc.h>
#include <src/common/pack.h>

#define UNIX_PATH_MAX 108    /* Cribbed from /usr/include/linux/un.h */

static int slurm_sign_auth_credentials( slurm_auth_credentials_t *cred );
static int open_authd_connection( void );
static int read_bytes( int fd, void *buf, size_t len );
static int write_bytes( int fd, void *buf, size_t len );

slurm_auth_credentials_t *
slurm_auth_alloc_credentials( void )
{
	return xmalloc( sizeof( slurm_auth_credentials_t ) );
}


void
slurm_auth_free_credentials( slurm_auth_credentials_t *cred )
{
	xfree( cred );
}


int
slurm_auth_activate_credentials( slurm_auth_credentials_t *cred,
				 time_t seconds_to_live )
{
/* By all rights we should use the library provided with authd
 * for doing this, which would make us more robust as authd's
 * implementation changes.  Unfortunately the authd library is
 * built on top of its author's convenience library, which we
 * don't want to require for SLURM users.
 */
  
	/* Initialize credentials with our user and group IDs. */
	cred->creds.uid = getuid();
	cred->creds.gid = getgid();
  
	/* Set the valid time interval. */
	cred->creds.valid_from = time( NULL );
	cred->creds.valid_to = cred->creds.valid_from + seconds_to_live;

	/* Sign the credentials. */
	return slurm_sign_auth_credentials( cred );
}


int
slurm_auth_verify_credentials( slurm_auth_credentials_t *cred )
{
#ifdef HAVE_AUTHD
	FILE *f;
	RSA *pub;
	int rc;
	time_t now;

	/* Open public key file.  XXX - want to configify this. */
	if ( ( f = fopen( AUTH_PUB_KEY, "r" ) ) == NULL ) {
		return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
	}

	/* Read key. */
	pub = PEM_read_RSA_PUBKEY( f, NULL, NULL, NULL );
	fclose( f );
	if ( pub == NULL ) return SLURM_PROTOCOL_AUTHENTICATION_ERROR;

	/* Verify signature. */
	ERR_load_crypto_strings();
	rc = RSA_verify( 0,
			 (unsigned char *) &cred->creds,
			 sizeof( credentials ),
			 cred->sig.data,
			 AUTH_RSA_SIGLEN,
			 pub );
	RSA_free( pub );

	/* RSA verification failed. */
	if ( rc == 0 ) return SLURM_PROTOCOL_AUTHENTICATION_ERROR;

	/* See if credential has expired. */
	now = time( NULL );
	if ( ( now >= cred->creds.valid_from )
	     && ( now <= cred->creds.valid_to ) )
		return SLURM_SUCCESS;
	else
		return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
#else
	return SLURM_SUCCESS;
#endif
}


/* Should really do this with inline accessors. */
int slurm_auth_uid( slurm_auth_credentials_t *cred )
{
	return cred->creds.uid;
}

int slurm_auth_gid( slurm_auth_credentials_t *cred )
{
	return cred->creds.gid;
}

void slurm_auth_pack_credentials( slurm_auth_credentials_t *cred,
				  void **buffer,
				  uint32_t *length )
{
	uint16_t chunk_size = sizeof( signature );
  
	pack32( cred->creds.uid, buffer, length );
	pack32( cred->creds.gid, buffer, length );
	pack32( cred->creds.valid_from, buffer, length );
	pack32( cred->creds.valid_to, buffer, length );
	packmem( cred->sig.data, chunk_size, buffer, length );
}


void slurm_auth_unpack_credentials( slurm_auth_credentials_t **cred_ptr,
				    void **buffer,
				    uint32_t *length )
{
	uint16_t dummy;
	char *data;
	slurm_auth_credentials_t *cred = xmalloc ( sizeof ( slurm_auth_credentials_t ) ) ;
  
	unpack32( &cred->creds.uid, buffer, length );
	unpack32( &cred->creds.gid, buffer, length );
	unpack32( (uint32_t *) &cred->creds.valid_from, buffer, length );
	unpack32( (uint32_t *) &cred->creds.valid_to, buffer, length );
	unpackmem_ptr( &data, &dummy, buffer, length );
	memcpy( cred->sig.data, data, sizeof( signature ) );
	*cred_ptr = cred ;
}


static int
slurm_sign_auth_credentials( slurm_auth_credentials_t *cred )
{
#ifdef HAVE_AUTHD
	int fd;

	/* Open connection to authd. */
	fd = open_authd_connection();
	if ( fd < 0 ) return SLURM_PROTOCOL_AUTHENTICATION_ERROR;

	/* Write credentials to socket. */
	if ( write_bytes( fd,
			  &cred->creds,
			  sizeof( credentials ) ) != SLURM_SUCCESS ) {
		close( fd );
		return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
	}
  
	/* Read response. */
	if ( read_bytes( fd,
			 &cred->sig,
			 sizeof( signature ) ) != SLURM_SUCCESS ) {
		close( fd );
		return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
	}
  
	close( fd );
	return SLURM_SUCCESS;
#else
	return SLURM_SUCCESS;
#endif
}

#if DEBUG
void
slurm_auth_print_credentials( slurm_auth_credentials_t *cred )
{
	struct passwd *pwent;
	struct group *grent;
	int i;
  
	printf( "-- BEGIN CLIENT CREDENTIALS\n" );
	pwent = getpwuid( cred->creds.uid );
	printf( "       user : %d (%s)\n",
		cred->creds.uid,
		pwent ? pwent->pw_name : "unknown" );
	grent = getgrgid( cred->creds.gid );
	printf( "      group : %d (%s)\n",
		cred->creds.gid,
		grent ? grent->gr_name : "unknown" );
  
	printf( "  effective : %ld %s",
		cred->creds.valid_from,
		ctime( &cred->creds.valid_from ) );
	printf( "    expires : %ld %s",
		cred->creds.valid_to,
		ctime( &cred->creds.valid_to ) );
	printf( "  signature :" );
	for ( i = 0; i < sizeof( cred->sig.data ); ++i ) {
		if ( ( i % 16 ) == 0 ) printf( "\n    " );
		if ( ( i % 4 ) == 0 ) putchar( ' ' );
		printf( "%02x", cred->sig.data[ i ] );
	}
	printf( "\n-- END CLIENT CREDENTIALS\n" );
}
#endif /*DEBUG*/

static int
open_authd_connection( void )
{
	int sock;
	char auth_sock_path[ UNIX_PATH_MAX ];
	struct sockaddr_un cl_addr;
	struct sockaddr_un sv_addr;

	sprintf( auth_sock_path, AUTH_SOCK_PATH, getpid() );

	sock = socket( AF_UNIX, SOCK_STREAM, 0 );
	if ( sock < 0 ) return SLURM_ERROR;

	cl_addr.sun_family = AF_UNIX;
	memset( cl_addr.sun_path, 0, UNIX_PATH_MAX );
	strncpy( &cl_addr.sun_path[ 1 ], auth_sock_path, UNIX_PATH_MAX - 1 );
	if ( bind( sock,
		   (struct sockaddr *) &cl_addr,
		   sizeof( struct sockaddr_un ) ) < 0 ) {
		close( sock );
		return SLURM_ERROR;
	}

	sv_addr.sun_family = AF_UNIX;
	memset( sv_addr.sun_path, 0, UNIX_PATH_MAX );
	strncpy( &sv_addr.sun_path[ 1 ], AUTHD_SOCK_PATH, UNIX_PATH_MAX - 1 );
	if ( connect( sock,
		      (struct sockaddr *) &sv_addr,
		      sizeof( struct sockaddr_un ) ) < 0 ) {
		close( sock );
		return SLURM_ERROR;
	}

	return sock;
}

static int read_bytes( int fd, void *buf, size_t n_bytes )
{
	size_t bytes_accumulated = 0;
	ssize_t bytes_read;

	while ( bytes_accumulated < n_bytes ) {
		bytes_read = read( fd,
				   (void *) ((unsigned long) buf +
					     (unsigned long) bytes_accumulated ),
				   n_bytes - bytes_accumulated );
		if ( bytes_read == 0 ) return 0;
		if ( bytes_read < 0 ) return -1;
		bytes_accumulated += bytes_read;
	}

	return 0;
}

static int write_bytes( int fd, void *buf, size_t n_bytes )
{
	size_t bytes_disposed = 0;
	ssize_t bytes_written;

	while ( bytes_disposed < n_bytes ) {
		bytes_written = write( fd,
				       (void *) ((unsigned long) buf +
						 (unsigned long) bytes_disposed ),
				       n_bytes - bytes_disposed );
		if ( bytes_written < 0 ) return -1;
		bytes_disposed += bytes_written;
	}

	return 0;
}

