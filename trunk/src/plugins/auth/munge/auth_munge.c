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
#  include <time.h>
#  include <stdint.h>
#  include <string.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>

#include <munge.h>

#include <slurm/slurm_errno.h>
#include "src/common/slurm_xlator.h"

#define MUNGE_ERRNO_OFFSET	1000

const char plugin_name[]       	= "auth plugin for Chris Dunlap's Munge";
const char plugin_type[]       	= "auth/munge";
const uint32_t plugin_version	= 10;

static int plugin_errno = SLURM_SUCCESS;

static int host_list_idx = -1;


enum {
	SLURM_AUTH_UNPACK = SLURM_AUTH_FIRST_LOCAL_ERROR
};

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
	int     len;       /* amount of App data                             */
	uid_t   uid;       /* UID. valid only if verified == true            */
	gid_t   gid;       /* GID. valid only if verified == true            */
	int cr_errno;
} slurm_auth_credential_t;

/*
 * Munge info structure for print* function
 */
typedef struct munge_info {
	time_t         encoded;
	time_t         decoded;
	munge_cipher_t cipher;
	munge_mac_t    mac;
	munge_zip_t    zip;
} munge_info_t;


/* Static prototypes
 */

static munge_info_t * cred_info_alloc(void);
static munge_info_t * cred_info_create(munge_ctx_t ctx);
static void           cred_info_destroy(munge_info_t *);
static void           _print_cred_info(munge_info_t *mi);
static void           _print_cred(munge_ctx_t ctx);
static int            _decode_cred(char *m, slurm_auth_credential_t *c);


/*
 *  Munge plugin initialization
 */
int init ( void )
{
	host_list_idx = arg_idx_by_name( slurm_auth_get_arg_desc(), 
			                 ARG_HOST_LIST );
	if (host_list_idx == -1) 
		return SLURM_ERROR;

	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}


/*
 * Allocate a credential.  This function should return NULL if it cannot
 * allocate a credential.  Whether the credential is populated with useful
 * data at this time is implementation-dependent.
 */
slurm_auth_credential_t *
slurm_auth_create( void *argv[] )
{
	int retry = 2;
	slurm_auth_credential_t *cred = NULL;
	munge_err_t e = EMUNGE_SUCCESS;
	munge_ctx_t ctx = munge_ctx_create();
	SigFunc *ohandler;

	if (ctx == NULL) {
		error("munge_ctx_create failure");
		return NULL;
	}

	cred = xmalloc(sizeof(*cred));
	cred->verified = false;
	cred->m_str    = NULL;
	cred->buf      = NULL;
	cred->len      = 0;
	cred->cr_errno = SLURM_SUCCESS;

	xassert(cred->magic = MUNGE_MAGIC);

	/*
	 *  Temporarily block SIGALARM to avoid misleading 
	 *    "Munged communication error" from libmunge if we 
	 *    happen to time out the connection in this secion of
	 *    code.
	 */
	ohandler = xsignal(SIGALRM, SIG_BLOCK);

    again:
	if ((e = munge_encode(&cred->m_str, ctx, cred->buf, cred->len))) {
		if (e == EMUNGE_SOCKET && retry--)
			goto again;

		error("Munge encode failed: %s", munge_ctx_strerror(ctx));
		xfree( cred );
		cred = NULL;
		plugin_errno = e + MUNGE_ERRNO_OFFSET;
	}

	xsignal(SIGALRM, ohandler);

	munge_ctx_destroy(ctx);

	return cred;
}

/*
 * Free a credential that was allocated with slurm_auth_alloc().
 */
int
slurm_auth_destroy( slurm_auth_credential_t *cred )
{
	if (!cred) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}

	xassert(cred->magic == MUNGE_MAGIC);

	xfree(cred->buf);

	/*
	 * Note: Munge cred not encoded with xmalloc()
	 */
	if (cred->m_str) free(cred->m_str);
	xfree(cred);
	return SLURM_SUCCESS;
}

/*
 * Verify a credential to approve or deny authentication.
 *
 * Return SLURM_SUCCESS if the credential is in order and valid.
 */
int
slurm_auth_verify( slurm_auth_credential_t *c, void *argv )
{
	if (!c) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}

	xassert(c->magic == MUNGE_MAGIC);

	if (c->verified) 
		return SLURM_SUCCESS;

	if (_decode_cred(c->m_str, c) < 0) 
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * Obtain the Linux UID from the credential.  The accuracy of this data
 * is not assured until slurm_auth_verify() has been called for it.
 */
uid_t
slurm_auth_get_uid( slurm_auth_credential_t *cred )
{
	if (cred == NULL) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_AUTH_NOBODY;
	}
	if (!cred->verified) {
		cred->cr_errno = SLURM_AUTH_INVALID;
		return SLURM_AUTH_NOBODY;
	}

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
	if (cred == NULL) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_AUTH_NOBODY;
	}
	if (!cred->verified) {
		cred->cr_errno = SLURM_AUTH_INVALID;
		return SLURM_AUTH_NOBODY;
	}

	xassert(cred->magic == MUNGE_MAGIC);

	return cred->gid;
}

/*
 * Marshall a credential for transmission over the network, according to
 * SLURM's marshalling protocol.
 */
int
slurm_auth_pack( slurm_auth_credential_t *cred, Buf buf )
{
	if (cred == NULL) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}
	if (buf == NULL) {
		cred->cr_errno = SLURM_AUTH_BADARG;
		return SLURM_ERROR;
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
	char    *type;
	char    *m;	
	uint16_t size;
	uint32_t version;
	
	if ( buf == NULL ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return NULL;
	}
	
	/*
	 * Get the authentication type.
	 */
	if ( unpackmem_ptr( &type, &size, buf ) != SLURM_SUCCESS ) {
		plugin_errno = SLURM_AUTH_UNPACK;
		return NULL;
	}
	if ( strcmp( type, plugin_type ) != 0 ) {
		plugin_errno = SLURM_AUTH_MISMATCH;
		return NULL;
	}
	if ( unpack32( &version, buf ) != SLURM_SUCCESS ) {
		plugin_errno = SLURM_AUTH_UNPACK;
		return NULL;
	}	
	if ( version != plugin_version ) {
		plugin_errno = SLURM_AUTH_MISMATCH;
		return NULL;
	}

	/* Allocate and initialize credential. */
	cred = xmalloc(sizeof(*cred));
	cred->verified = false;
	cred->m_str    = NULL;
	cred->buf      = NULL;
	cred->len      = 0;
	cred->cr_errno = SLURM_SUCCESS;

	xassert(cred->magic = MUNGE_MAGIC);

	if (unpackmem_ptr(&m, &size, buf) < 0) {
		plugin_errno = SLURM_AUTH_UNPACK;
		goto unpack_error;
	}

	if (_decode_cred(m, cred) < 0) 
		goto unpack_error;

	return cred;

 unpack_error:
	xfree( cred );
	return NULL;
}

/*
 * Print to a stdio stream a human-readable representation of the
 * credential for debugging or logging purposes.  The format is left
 * to the imagination of the plugin developer.
 */
int
slurm_auth_print( slurm_auth_credential_t *cred, FILE *fp )
{
	if (cred == NULL) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}
	if ( fp == NULL ) {
		cred->cr_errno = SLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}

	fprintf(fp, "BEGIN SLURM MUNGE AUTHENTICATION CREDENTIAL\n" );
	fprintf(fp, "%s\n", cred->m_str );
	fprintf(fp, "END SLURM MUNGE AUTHENTICATION CREDENTIAL\n" );
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
		{ 0, NULL }
	};

	int i;

	if (slurm_errno > MUNGE_ERRNO_OFFSET)
		return munge_strerror(slurm_errno);

	for ( i = 0; ; ++i ) {
		if ( tbl[ i ].msg == NULL ) return "unknown error";
		if ( tbl[ i ].err == slurm_errno ) return tbl[ i ].msg;
	}
}


/*
 * Decode the munge encoded credential `m' placing results, if validated,
 * into slurm credential `c'
 */
static int 
_decode_cred(char *m, slurm_auth_credential_t *c)
{
	int retry = 2;
	munge_err_t e;
	munge_ctx_t ctx;

	if ((c == NULL) || (m == NULL)) 
		return SLURM_ERROR;

	xassert(c->magic == MUNGE_MAGIC);

	if (c->verified) 
		return SLURM_SUCCESS;

	if ((ctx = munge_ctx_create()) == NULL) {
		error("munge_ctx_create failure");
		return SLURM_ERROR;
	}

    again:
	if ((e = munge_decode(m, ctx, &c->buf, &c->len, &c->uid, &c->gid))) {
		error ("Munge decode failed: %s %s", 
			munge_ctx_strerror(ctx), retry ? "(retrying ...)": "");

		if ((e == EMUNGE_SOCKET) && retry--)
			goto again;

		/*
		 *  Print any valid credential data 
		 */
		_print_cred(ctx); 

		plugin_errno = e + MUNGE_ERRNO_OFFSET;

		goto done;
	}

	c->verified = true;

     done:
	munge_ctx_destroy(ctx);

	return e ? SLURM_ERROR : SLURM_SUCCESS;
}



/*
 *  Allocate space for Munge credential info structure
 */
static munge_info_t *
cred_info_alloc(void)
{
	munge_info_t *mi = xmalloc(sizeof(*mi));
	memset(mi, 0, sizeof(*mi));
	return mi;
}

/*
 *  Free a Munge cred info object.
 */
static void
cred_info_destroy(munge_info_t *mi)
{
	xfree(mi);
}

/*
 *  Create a credential info object from a Munge context
 */
static munge_info_t *
cred_info_create(munge_ctx_t ctx)
{
	munge_err_t e;
	munge_info_t *mi = cred_info_alloc();

	e = munge_ctx_get(ctx, MUNGE_OPT_ENCODE_TIME, &mi->encoded);
	if (e != EMUNGE_SUCCESS)
		error ("auth_munge: Unable to retrieve encode time: %s",
		       munge_ctx_strerror(ctx));

	e = munge_ctx_get(ctx, MUNGE_OPT_DECODE_TIME, &mi->decoded);
	if (e != EMUNGE_SUCCESS)
		error ("auth_munge: Unable to retrieve decode time: %s",
		       munge_ctx_strerror(ctx));

	e = munge_ctx_get(ctx, MUNGE_OPT_CIPHER_TYPE, &mi->cipher);
	if (e != EMUNGE_SUCCESS)
		error ("auth_munge: Unable to retrieve cipher type: %s",
		       munge_ctx_strerror(ctx));

	e = munge_ctx_get(ctx, MUNGE_OPT_MAC_TYPE, &mi->mac);
	if (e != EMUNGE_SUCCESS)
		error ("auth_munge: Unable to retrieve mac type: %s",
		       munge_ctx_strerror(ctx));

	e = munge_ctx_get(ctx, MUNGE_OPT_ZIP_TYPE, &mi->zip);
	if (e != EMUNGE_SUCCESS)
		error ("auth_munge: Unable to retrieve zip type: %s",
		       munge_ctx_strerror(ctx));

	return mi;
}


/*
 *  Print credential info object to the slurm log facility.
 */
static void
_print_cred_info(munge_info_t *mi)
{
	char buf[256];

	xassert(mi != NULL);

	if (mi->encoded > 0)
		info ("ENCODED: %s", ctime_r(&mi->encoded, buf));

	if (mi->decoded > 0)
		info ("DECODED: %s", ctime_r(&mi->decoded, buf));
}


/*
 *  Print credential information.
 */
static void 
_print_cred(munge_ctx_t ctx)
{
	munge_info_t *mi = cred_info_create(ctx);
	_print_cred_info(mi);
	cred_info_destroy(mi);
}

