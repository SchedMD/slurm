/*****************************************************************************\
 *  auth_munge.c - Slurm auth implementation via Chris Dunlap's Munge
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>
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

#include "config.h"

#include <inttypes.h>
#include <munge.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/common/slurm_time.h"

#define MUNGE_ERRNO_OFFSET	1000
#define RETRY_COUNT		20
#define RETRY_USEC		100000

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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]       	= "Munge authentication plugin";
const char plugin_type[]       	= "auth/munge";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static int plugin_errno = SLURM_SUCCESS;
static int bad_cred_test = -1;


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

static char *         _auth_opts_to_socket(char *opts);
static munge_info_t * cred_info_alloc(void);
static munge_info_t * cred_info_create(munge_ctx_t ctx);
static void           cred_info_destroy(munge_info_t *);
static void           _print_cred_info(munge_info_t *mi);
static void           _print_cred(munge_ctx_t ctx);
static int            _decode_cred(slurm_auth_credential_t *c, char *socket);

/*
 *  Munge plugin initialization
 */
int init ( void )
{
	char *fail_test_env = getenv("SLURM_MUNGE_AUTH_FAIL_TEST");
	if (fail_test_env)
		bad_cred_test = atoi(fail_test_env);
	else
		bad_cred_test = 0;

	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}


/*
 * Allocate a credential.  This function should return NULL if it cannot
 * allocate a credential.  Whether the credential is populated with useful
 * data at this time is implementation-dependent.
 */
slurm_auth_credential_t *slurm_auth_create(char *opts)
{
	int rc, retry = RETRY_COUNT, auth_ttl;
	slurm_auth_credential_t *cred = NULL;
	munge_err_t err = EMUNGE_SUCCESS;
	munge_ctx_t ctx = munge_ctx_create();
	SigFunc *ohandler;
	char *socket;

	if (ctx == NULL) {
		error("munge_ctx_create failure");
		return NULL;
	}

#if 0
	/* This logic can be used to determine what socket is used by default.
	 * A typical name is "/var/run/munge/munge.socket.2" */
{
	char *old_socket;
	if (munge_ctx_get(ctx, MUNGE_OPT_SOCKET, &old_socket) != EMUNGE_SUCCESS)
		error("munge_ctx_get failure");
	else
		info("Default Munge socket is %s", old_socket);
}
#endif

	if (opts) {
		socket = _auth_opts_to_socket(opts);
		rc = munge_ctx_set(ctx, MUNGE_OPT_SOCKET, socket);
		xfree(socket);
		if (rc != EMUNGE_SUCCESS) {
			error("munge_ctx_set failure");
			munge_ctx_destroy(ctx);
			return NULL;
		}
	}

	auth_ttl = slurm_get_auth_ttl();
	if (auth_ttl)
		(void) munge_ctx_set(ctx, MUNGE_OPT_TTL, auth_ttl);

	cred = xmalloc(sizeof(*cred));
	cred->verified = false;
	cred->m_str    = NULL;
	cred->buf      = NULL;
	cred->len      = 0;
	cred->cr_errno = SLURM_SUCCESS;

	xassert((cred->magic = MUNGE_MAGIC));

	/*
	 *  Temporarily block SIGALARM to avoid misleading
	 *    "Munged communication error" from libmunge if we
	 *    happen to time out the connection in this secion of
	 *    code. FreeBSD needs this cast.
	 */
	ohandler = xsignal(SIGALRM, (SigFunc *)SIG_BLOCK);

again:
	err = munge_encode(&cred->m_str, ctx, cred->buf, cred->len);
	if (err != EMUNGE_SUCCESS) {
		if ((err == EMUNGE_SOCKET) && retry--) {
			debug("Munge encode failed: %s (retrying ...)",
			      munge_ctx_strerror(ctx));
			usleep(RETRY_USEC);	/* Likely munged too busy */
			goto again;
		}
		if (err == EMUNGE_SOCKET)
			error("If munged is up, restart with --num-threads=10");
		error("Munge encode failed: %s", munge_ctx_strerror(ctx));
		xfree( cred );
		cred = NULL;
		plugin_errno = err + MUNGE_ERRNO_OFFSET;
	} else if ((bad_cred_test > 0) && cred->m_str) {
		int i = ((int) time(NULL)) % strlen(cred->m_str);
		cred->m_str[i]++;	/* random position in credential */
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

	/*
	 * Note: Munge cred string and application-specific data in
	 *  "buf" not encoded with xmalloc()
	 */
	if (cred->m_str)
		free(cred->m_str);
	if (cred->buf)
		free(cred->buf);

	xfree(cred);
	return SLURM_SUCCESS;
}

/*
 * Verify a credential to approve or deny authentication.
 *
 * Return SLURM_SUCCESS if the credential is in order and valid.
 */
int
slurm_auth_verify( slurm_auth_credential_t *c, char *opts )
{
	int rc;
	char *socket;

	if (!c) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}

	xassert(c->magic == MUNGE_MAGIC);

	if (c->verified)
		return SLURM_SUCCESS;

	socket = _auth_opts_to_socket(opts);
	rc = _decode_cred(c, socket);
	xfree(socket);
	if (rc < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * Obtain the Linux UID from the credential.  The accuracy of this data
 * is not assured until slurm_auth_verify() has been called for it.
 */
uid_t
slurm_auth_get_uid( slurm_auth_credential_t *cred, char *opts )
{
	if (cred == NULL) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_AUTH_NOBODY;
	}

	if (!cred->verified) {
		int rc;
		char *socket = _auth_opts_to_socket(opts);
		rc = _decode_cred(cred, socket);
		xfree(socket);
		if (rc < 0) {
			cred->cr_errno = SLURM_AUTH_INVALID;
			return SLURM_AUTH_NOBODY;
		}
	}

	xassert(cred->magic == MUNGE_MAGIC);

	return cred->uid;
}

/*
 * Obtain the Linux GID from the credential.  See slurm_auth_get_uid()
 * above for details on correct behavior.
 */
gid_t
slurm_auth_get_gid( slurm_auth_credential_t *cred, char *opts )
{
	if (cred == NULL) {
		plugin_errno = SLURM_AUTH_BADARG;
		return SLURM_AUTH_NOBODY;
	}

	if (!cred->verified) {
		int rc;
		char *socket = _auth_opts_to_socket(opts);
		rc = _decode_cred(cred, socket);
		xfree(socket);
		if (rc < 0) {
			cred->cr_errno = SLURM_AUTH_INVALID;
			return SLURM_AUTH_NOBODY;
		}
	}

	xassert(cred->magic == MUNGE_MAGIC);

	return cred->gid;
}

/*
 * Marshall a credential for transmission over the network, according to
 * Slurm's marshalling protocol.
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
 * to Slurm's marshalling protocol.
 */
slurm_auth_credential_t *
slurm_auth_unpack( Buf buf )
{
	slurm_auth_credential_t *cred = NULL;
	char    *type;
	uint32_t size;
	uint32_t version;

	if ( buf == NULL ) {
		plugin_errno = SLURM_AUTH_BADARG;
		return NULL;
	}

	/*
	 * Get the authentication type.
	 */
	safe_unpackmem_ptr( &type, &size, buf );

	if (( type == NULL ) ||
	    ( xstrcmp( type, plugin_type ) != 0 )) {
		debug("slurm_auth_unpack error: packed by %s unpack by %s",
		      type, plugin_type);
		plugin_errno = SLURM_AUTH_MISMATCH;
		return NULL;
	}
	safe_unpack32( &version, buf );

	/* Allocate and initialize credential. */
	cred = xmalloc(sizeof(*cred));
	cred->verified = false;
	cred->m_str    = NULL;
	cred->buf      = NULL;
	cred->len      = 0;
	cred->cr_errno = SLURM_SUCCESS;

	xassert((cred->magic = MUNGE_MAGIC));

	safe_unpackstr_malloc(&cred->m_str, &size, buf);
	return cred;

 unpack_error:
	plugin_errno = SLURM_AUTH_UNPACK;
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
		return munge_strerror(slurm_errno - MUNGE_ERRNO_OFFSET);

	for ( i = 0; ; ++i ) {
		if ( tbl[ i ].msg == NULL )
			return "unknown error";
		if ( tbl[ i ].err == slurm_errno )
			return tbl[ i ].msg;
	}
}


/*
 * Decode the munge encoded credential `m_str' placing results, if validated,
 * into slurm credential `c'
 */
static int
_decode_cred(slurm_auth_credential_t *c, char *socket)
{
	int retry = RETRY_COUNT;
	munge_err_t err;
	munge_ctx_t ctx;

	if (c == NULL)
		return SLURM_ERROR;

	xassert(c->magic == MUNGE_MAGIC);

	if (c->verified)
		return SLURM_SUCCESS;

	if ((ctx = munge_ctx_create()) == NULL) {
		error("munge_ctx_create failure");
		return SLURM_ERROR;
	}
	if (socket &&
	    (munge_ctx_set(ctx, MUNGE_OPT_SOCKET, socket) != EMUNGE_SUCCESS)) {
		error("munge_ctx_set failure");
		munge_ctx_destroy(ctx);
		return SLURM_ERROR;
	}

    again:
	c->buf = NULL;
	err = munge_decode(c->m_str, ctx, &c->buf, &c->len, &c->uid, &c->gid);
	if (err != EMUNGE_SUCCESS) {
		if (c->buf) {
			free(c->buf);
			c->buf = NULL;
		}
		if ((err == EMUNGE_SOCKET) && retry--) {
			debug("Munge decode failed: %s (retrying ...)",
			      munge_ctx_strerror(ctx));
			usleep(RETRY_USEC);	/* Likely munged too busy */
			goto again;
		}
		if (err == EMUNGE_SOCKET)
			error("If munged is up, restart with --num-threads=10");
#ifdef MULTIPLE_SLURMD
		/*
		 * In multiple slurmd mode this will happen all the time since
		 * we are authenticating with the same munged. It can also
		 * happen if slurmctld and slurmd are on the same node and
		 * message aggregation is configured (error is recoverable).
		 */
		if (err != EMUNGE_CRED_REPLAYED) {
#endif
			/*
			 *  Print any valid credential data
			 */
			error ("Munge decode failed: %s",
			       munge_ctx_strerror(ctx));
			_print_cred(ctx);
			if (err == EMUNGE_CRED_REWOUND)
				error("Check for out of sync clocks");
			c->cr_errno = err + MUNGE_ERRNO_OFFSET;
#ifdef MULTIPLE_SLURMD
		} else {
			debug2("We had a replayed cred, "
			       "but this is expected in multiple "
			       "slurmd mode.");
			err = 0;
		}
#endif
		goto done;
	}

	c->verified = true;

     done:
	munge_ctx_destroy(ctx);
	return err ? SLURM_ERROR : SLURM_SUCCESS;
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
		info ("ENCODED: %s", slurm_ctime2_r(&mi->encoded, buf));

	if (mi->decoded > 0)
		info ("DECODED: %s", slurm_ctime2_r(&mi->decoded, buf));
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

/*
 * Convert AuthInfo to a socket path. Accepts two input formats:
 * 1) <path>		(Old format)
 * 2) socket=<path>[,]	(New format)
 * NOTE: Caller must xfree return value
 */
static char *_auth_opts_to_socket(char *opts)
{
	char *socket = NULL, *sep, *tmp;

	if (!opts)
		return NULL;

	tmp = strstr(opts, "socket=");
	if (tmp) {	/* New format */
		socket = xstrdup(tmp + 7);
		sep = strchr(socket, ',');
		if (sep)
			sep[0] = '\0';
	} else if (strchr(opts, '='))
		;	/* New format, but socket not specified */
	else
		socket = xstrdup(opts);	/* Old format */

	return socket;
}
