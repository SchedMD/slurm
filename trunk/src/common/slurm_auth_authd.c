/*****************************************************************************\
 * slurm_auth_authd.c - authentication module for Brent Chun's authd
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
#  include <config.h>
#endif


#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>

#if HAVE_SSL
#  include <openssl/rsa.h>
#  include <openssl/pem.h>
#  include <openssl/err.h>
#endif

/*
 * Because authd's authentication scheme relies on determining the
 * owner of a Unix domain socket over which the request is made to
 * authenticate credentials, we must do our own socket thing unless
 * the transport abstraction layer will provide for Unix domain
 * sockets.
 */
#include <sys/socket.h>
#include <sys/un.h>

#include <src/common/slurm_errno.h>
#include <src/common/xmalloc.h>
#include <src/common/pack.h>
#include <src/common/log.h>
#include <src/common/slurm_auth.h>

#define UNIX_PATH_MAX 108    /* Cribbed from /usr/include/linux/un.h */

#if HAVE_AUTHD
#  include <auth.h>
/* Completion of slurm_auth_credentials_t from slurm_auth.h 
 */
typedef struct slurm_auth_credentials {
	credentials creds;	/* Authd's credential structure. */
	signature sig;		/* RSA hash for the credentials. */
} slurm_auth_credentials_t;
#else
typedef struct credentials {
	uid_t uid;
	gid_t gid;
	time_t valid_from;
	time_t valid_to;
}
typedef struct slurm_auth_credentials {
	credentials creds;
} slurm_auth_credentials_t;
#endif

static int slurm_sign_auth_credentials( slurm_auth_credentials_t *cred );

slurm_auth_t 
slurm_auth_alloc_credentials( void )
{
	return (slurm_auth_t) xmalloc(sizeof(struct slurm_auth_credentials));
}


void
slurm_auth_free_credentials( slurm_auth_t cred )
{
	xfree( cred );
}


int
slurm_auth_activate_credentials( slurm_auth_t cred, time_t ttl )
{
	int rc;
	/* Initialize credentials with our user and group IDs. */
	cred->creds.uid = getuid();
	cred->creds.gid = getgid();
  
	/* Set the valid time interval. */
	cred->creds.valid_from = time( NULL );
	cred->creds.valid_to = cred->creds.valid_from + ttl;

	/* Sign the credentials. */
	if ((rc = slurm_sign_auth_credentials(cred)) < 0) 
		return SLURM_FAILURE;
		
	return SLURM_SUCCESS;
}


int
slurm_auth_verify_credentials( slurm_auth_t cred )
{
#ifdef HAVE_AUTHD
	int rc = auth_verify_signature(&cred->creds, &cred->sig);
	if (rc < 0)
		return SLURM_FAILURE;
#else
	return SLURM_SUCCESS;
#endif
}


/* Should really do this with inline accessors. */
uid_t slurm_auth_uid( slurm_auth_t cred )
{
	return cred->creds.uid;
}

gid_t slurm_auth_gid( slurm_auth_t cred )
{
	return cred->creds.gid;
}

void slurm_auth_pack_credentials( slurm_auth_t cred,
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


void slurm_auth_unpack_credentials( slurm_auth_t *credp,
				    void **buffer,
				    uint32_t *length )
{
	uint16_t dummy;
	char *data;
	slurm_auth_t cred;

	cred = slurm_auth_alloc_credentials();
	unpack32( &cred->creds.uid, buffer, length );
	unpack32( &cred->creds.gid, buffer, length );
	unpack32( (uint32_t *) &cred->creds.valid_from, buffer, length );
	unpack32( (uint32_t *) &cred->creds.valid_to, buffer, length );
	unpackmem_ptr( &data, &dummy, buffer, length );
	memcpy( cred->sig.data, data, sizeof( signature ) );
	*credp = cred;
	return;
}


static int
slurm_sign_auth_credentials( slurm_auth_t cred )
{
#ifdef HAVE_AUTHD
	return auth_get_signature(&cred->creds, &cred->sig);
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
