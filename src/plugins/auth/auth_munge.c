/*****************************************************************************\
 *  auth_munge.c - SLURM auth implementation via Chris Dunlap's Munge
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov> 
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
#include <stdlib.h>

#include <munge.h>

#include <slurm/slurm_errno.h>

#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/pack.h"
#include "src/common/log.h"
#include "src/common/slurm_auth.h"

const char plugin_name[]       	= "auth plugin for Chris Dunlap's Munge";
const char plugin_type[]       	= "auth/munge";
const uint32_t plugin_version	= 10;

/* 
 * The Munge implementation of the slurm AUTH credential
 */
typedef struct _slurm_auth_credential {
#ifndef NDEBUG
#       define MUNGE_MAGIC 0xfeed
	int  magic;        /* magical munge validity magic                   */
#endif
	char   *m_str;     /* munged string                                  */
	void   *buf;       /* Application specific data                      */
	bool    verified;  /* true if this cred has been verified            */
	size_t  len;       /* amount of App data                             */
	uid_t   uid;       /* UID. valid only if verified == true            */
	gid_t   gid;       /* GID. valid only if verified == true            */ 
} slurm_auth_credential_t;

/*
 * Decode the munge encoded credential `m' placing results, if validated,
 * into slurm credential `c'
 */
static int 
_decode_cred(char *m, slurm_auth_credential_t *c)
{
	munge_err_t e;
	munge_ctx_t *ctx = NULL;

	if ((c == NULL) || (m == NULL)) 
		return SLURM_ERROR;

	xassert(c->magic == MUNGE_MAGIC);

	if (c->verified) 
		return SLURM_SUCCESS;

	if ((e = munge_decode(m, ctx, &c->buf, &c->len, &c->uid, &c->gid))) {
		error ("Invalid Munge credential: %s", munge_strerror(e));
		return SLURM_ERROR;
	}

	c->verified = true;
	return SLURM_SUCCESS;
}


int init ( void )
{
	/* 
	 * Perhaps we could init a global context here? 
	 * Do nothing for now.
	 *
	 */
	return 0;
}


/*
 * Allocate a credential.  This function should return NULL if it cannot
 * allocate a credential.  Whether the credential is populated with useful
 * data at this time is implementation-dependent.
 */
slurm_auth_credential_t *
slurm_auth_alloc( void )
{
	slurm_auth_credential_t *cred = NULL;

	cred = xmalloc(sizeof(*cred));
	cred->verified = false;
	cred->m_str    = NULL;
	cred->buf      = NULL;
	cred->len      = 0;

	xassert(cred->magic = MUNGE_MAGIC);

	return cred;
}

/*
 * Free a credential that was allocated with slurm_auth_alloc().
 */
void
slurm_auth_free( slurm_auth_credential_t *cred )
{
	if (!cred) return;

	xassert(cred->magic == MUNGE_MAGIC);

	xfree(cred->buf);

	/*
	 * Note: Munge cred not encoded with xmalloc()
	 */
	if (cred->m_str) free(cred->m_str);
	xfree(cred);
}

/*
 * Prepare a credential for use as an authentication token.  Accessor
 * functions (slurm_auth_get_uid() and slurm_auth_get_gid()) are not required
 * to return valid data until this function has been called successfully
 * for the credential.
 *
 * secs - the number of seconds for which this credential is deemed
 * valid; not appropriate to this plugin but essential to others.
 *
 * Return SLURM_SUCCESS if the credential is successfully activated.
 */
int
slurm_auth_activate( slurm_auth_credential_t *cred, int secs )
{
	munge_err_t e = EMUNGE_SUCCESS; 
	munge_ctx_t *ctx = NULL;

	if (!cred) return SLURM_ERROR;

	xassert(cred->magic == MUNGE_MAGIC);

	if ((e = munge_encode(&cred->m_str, ctx, cred->buf, cred->len))) {
		error("munge_encode: %s", munge_strerror(e));
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * Verify a credential to approve or deny authentication.
 *
 * Return SLURM_SUCCESS if the credential is in order and valid.
 */
int
slurm_auth_verify( slurm_auth_credential_t *c )
{
	if (!c) return SLURM_ERROR;

	xassert(c->magic == MUNGE_MAGIC);

	if (c->verified) 
		return SLURM_SUCCESS;

	if (_decode_cred(c->m_str, c) < 0) 
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * Obtain the Linux UID from the credential.  The accuracy of this data
 * is not assured until slurm_auth_activate() and slurm_auth_verify()
 * have been called for it in that order.  A plugin may choose to
 * enforce this call order by adding bookkeeping to its implementation
 * of slurm_auth_credential_t, but SLURM will guarantee to issue the
 * proper sequence of calls.
 */
uid_t
slurm_auth_get_uid( slurm_auth_credential_t *cred )
{
	if ((cred == NULL) || !cred->verified) 
		return SLURM_AUTH_NOBODY;

	xassert(cred->magic == MUNGE_MAGIC);

	return cred->uid;
}

/*
 * Obtain the Linux GID from the credential.  See slurm_auth_get_uid()
 * above for details on correct behavior.
 */
gid_t
slurm_auth_get_gid( slurm_auth_credential_t *cred )
{
	if ((cred == NULL) || !cred->verified) 
		return SLURM_AUTH_NOBODY;

	xassert(cred->magic == MUNGE_MAGIC);

	return cred->gid;
}

/*
 * Marshall a credential for transmission over the network, according to
 * SLURM's marshalling protocol.
 */
void
slurm_auth_pack( slurm_auth_credential_t *cred, Buf buf )
{
	if ((cred == NULL) || (buf == NULL)) {
		error( "malformed slurm_auth_pack call in auth/munge plugin" );
		return;
	}
	
	xassert(cred->magic == MUNGE_MAGIC);

	/*
	 * Prefix the credential with a description of the credential
	 * type so that it can be sanity-checked at the receiving end.
	 */
	packstr( (char *) plugin_type, buf );
	pack32( plugin_version, buf );
	/*
	 * Pack the data.
	 */
	packstr(cred->m_str, buf);
}

/*
 * Unmarshall a credential after transmission over the network according
 * to SLURM's marshalling protocol.
 */
int
slurm_auth_unpack( slurm_auth_credential_t *cred, Buf buf )
{
	char    *type;
	char    *m;	
	uint16_t size;
	uint32_t version;
	
	if ((cred == NULL) || (buf == NULL)) {
		error("malformed slurm_auth_unpack call in auth/munge plugin");
		return SLURM_ERROR;
	}
	
	/*
	 * Get the authentication type.
	 */
	if ( unpackmem_ptr( &type, &size, buf ) != SLURM_SUCCESS ) {
		error( "cannot unpack authentication type: %m" );
		return SLURM_ERROR;
	}
	if ( strcmp( type, plugin_type ) != 0 ) {
		error( "runtime authentication plugin type mismatch" );
		error( "plugin expected 'auth/munge' and got '%s'", type );
		return SLURM_ERROR;
	}
	if ( unpack32( &version, buf ) != SLURM_SUCCESS ) {
		return SLURM_ERROR;
	}	

	if (unpackmem_ptr(&m, &size, buf) < 0) {
		error("error retrieving munge cred in auth/munge plugin");
		return SLURM_ERROR;
	}

	if (_decode_cred(m, cred) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;

}

/*
 * Print to a stdio stream a human-readable representation of the
 * credential for debugging or logging purposes.  The format is left
 * to the imagination of the plugin developer.
 */
void
slurm_auth_print( slurm_auth_credential_t *cred, FILE *fp )
{
	if ((cred == NULL) || (fp == NULL)) 
		return;

	fprintf(fp, "BEGIN SLURM MUNGE AUTHENTICATION CREDENTIAL\n" );
	fprintf(fp, "%s\n", cred->m_str );
	fprintf(fp, "END SLURM MUNGE AUTHENTICATION CREDENTIAL\n" );
}
