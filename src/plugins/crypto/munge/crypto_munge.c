/*****************************************************************************\
 *  crypto_munge.c - Munge based cryptographic signature plugin
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
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
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else /* ! HAVE_INTTYPES_H */
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif /* HAVE_INTTYPES_H */
#else /* ! HAVE_CONFIG_H */
#  include <stdint.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GPL_LICENSED 1
#include <munge.h>

#include <slurm/slurm_errno.h>
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

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
const char plugin_name[]        = "Munge cryptographic signature plugin";
const char plugin_type[]        = "crypto/munge";
const uint32_t plugin_version   = 90;


/*
 *  Error codes local to this plugin:
 */
enum local_error_code {
	ESIG_BUF_DATA_MISMATCH = 5000,
	ESIG_BUF_SIZE_MISMATCH,
	ESIG_BAD_USERID,
};

static uid_t slurm_user = 0;

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
 * fini() is called when the plugin is unloaded, 
 * free any global memory allocations here to avoid memory leaks.
 */
extern int fini ( void )
{
	verbose("%s unloaded", plugin_name);
	return SLURM_SUCCESS;
}

extern void
crypto_destroy_key(void *key)
{
	munge_ctx_destroy((munge_ctx_t) key);
	return;
}

extern void *
crypto_read_private_key(const char *path)
{
	munge_ctx_t ctx;
	munge_err_t err;

	if ((ctx = munge_ctx_create()) == NULL) {
		error ("crypto_read_private_key: munge_ctx_create failed");
		return (NULL);
	}

	/*
	 *   Only allow slurmd_user (usually root) to decode job
	 *   credentials created by
	 *   slurmctld. This provides a slight layer of extra security,
	 *   as non-privileged users cannot get at the contents of job
	 *   credentials.
	 */
	err = munge_ctx_set(ctx, MUNGE_OPT_UID_RESTRICTION, 
			    slurm_get_slurmd_user_id());

	if (err != EMUNGE_SUCCESS) {
		error("Unable to set uid restriction on munge credentials: %s",
		      munge_ctx_strerror (ctx));
		munge_ctx_destroy(ctx);
		return(NULL);
	}

	return ((void *) ctx);
}

extern void *
crypto_read_public_key(const char *path)
{
	/*
	 * Get slurm user id once. We use it later to verify credentials.
	 */
	slurm_user = slurm_get_slurm_user_id();

	return (void *) munge_ctx_create();
}

extern const char *
crypto_str_error(int errnum)
{
	if (errnum == ESIG_BUF_DATA_MISMATCH)
		return "Credential data mismatch";
	else if (errnum == ESIG_BUF_SIZE_MISMATCH)
		return "Credential data size mismatch";
	else if (errnum == ESIG_BAD_USERID)
		return "Credential created by invalid user";
	else
		return munge_strerror ((munge_err_t) errnum);
}

/* NOTE: Caller must xfree the signature returned by sig_pp */
extern int
crypto_sign(void * key, char *buffer, int buf_size, char **sig_pp, 
	    unsigned int *sig_size_p) 
{
	char *cred;
	munge_err_t err;

	err = munge_encode(&cred, (munge_ctx_t) key,
			   buffer, buf_size);

	if (err != EMUNGE_SUCCESS)
		return err;

	*sig_size_p = strlen(cred) + 1;
	*sig_pp = xstrdup(cred);
	free(cred); 
	return 0;
}

extern int
crypto_verify_sign(void * key, char *buffer, unsigned int buf_size, 
		   char *signature, unsigned int sig_size)
{
	uid_t uid;
	gid_t gid;
	void *buf_out;
	int   buf_out_size;
	int   rc = 0;
	munge_err_t err;

	err = munge_decode(signature, (munge_ctx_t) key,
			   &buf_out, &buf_out_size, 
			   &uid, &gid);

	if (err != EMUNGE_SUCCESS) {
#ifdef MULTIPLE_SLURMD
		/* In multple slurmd mode this will happen all the
		 * time since we are authenticating with the same
		 * munged.
		 */
		if (err != EMUNGE_CRED_REPLAYED) {
			return err;
		} else {
			debug2("We had a replayed crypto, "
			       "but this is expected in multiple "
			       "slurmd mode.");
		}
#else
		return err;
#endif
	}


	if ((uid != slurm_user) && (uid != 0)) {
		error("crypto/munge: Unexpected uid (%d) != SLURM uid (%d)",
		      (int) uid, (int) slurm_user);
		rc = ESIG_BAD_USERID;
	}
	else if (buf_size != buf_out_size)
		rc = ESIG_BUF_SIZE_MISMATCH;
	else if (memcmp(buffer, buf_out, buf_size))
		rc = ESIG_BUF_DATA_MISMATCH;

	free(buf_out);
	return rc;
}
