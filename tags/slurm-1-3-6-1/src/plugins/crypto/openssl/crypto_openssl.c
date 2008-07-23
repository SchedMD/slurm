/*****************************************************************************\
 *  crypto_openssl.c - OpenSSL based cryptographic signature plugin
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

/*
 * OpenSSL includes
 */
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>

#include <slurm/slurm_errno.h>
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

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
const char plugin_name[]        = "OpenSSL cryptographic signature plugin";
const char plugin_type[]        = "crypto/openssl";
const uint32_t plugin_version   = 90;

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
	if (key)
		EVP_PKEY_free((EVP_PKEY *) key);
}

extern void *
crypto_read_private_key(const char *path)
{
	FILE     *fp = NULL;
	EVP_PKEY *pk = NULL;

	xassert(path != NULL);

	if (!(fp = fopen(path, "r")))
		return NULL;

	if (!PEM_read_PrivateKey(fp, &pk, NULL, NULL)) {
		fclose(fp);
		return NULL;
	}
	fclose(fp);

	return (void *) pk;
}


extern void *
crypto_read_public_key(const char *path)
{
	FILE     *fp = NULL;
	EVP_PKEY *pk = NULL;

	xassert(path != NULL);

	if ((fp = fopen(path, "r")) == NULL)
		return NULL;

	if (!PEM_read_PUBKEY(fp, &pk, NULL, NULL)) {
		fclose(fp);
		return NULL;
	} 
	fclose(fp);

	return (void *) pk;
}

extern char *
crypto_str_error(void)
{
	static int loaded = 0;

	if (loaded == 0) {
		ERR_load_crypto_strings();
		loaded = 1;
	}

	return (char *) ERR_reason_error_string(ERR_get_error()); 
}

/* NOTE: Caller must xfree the signature returned by sig_pp */
extern int
crypto_sign(void * key, char *buffer, int buf_size, char **sig_pp, 
		unsigned int *sig_size_p) 
{
	EVP_MD_CTX    ectx;
	int           rc    = SLURM_SUCCESS;
	int           ksize = EVP_PKEY_size((EVP_PKEY *) key);

	/*
	 * Allocate memory for signature: at most EVP_PKEY_size() bytes
	 */
	*sig_pp = xmalloc(ksize * sizeof(unsigned char));

	EVP_SignInit(&ectx, EVP_sha1());
	EVP_SignUpdate(&ectx, buffer, buf_size);

	if (!(EVP_SignFinal(&ectx, (unsigned char *)*sig_pp, sig_size_p, 
			(EVP_PKEY *) key))) {
		rc = SLURM_ERROR;
	}

#ifdef HAVE_EVP_MD_CTX_CLEANUP
	/* Note: Likely memory leak if this function is absent */
	EVP_MD_CTX_cleanup(&ectx);
#endif

	return rc;
}

extern int
crypto_verify_sign(void * key, char *buffer, unsigned int buf_size, 
		char *signature, unsigned int sig_size)
{
	EVP_MD_CTX     ectx;
	int            rc;

	EVP_VerifyInit(&ectx, EVP_sha1());
	EVP_VerifyUpdate(&ectx, buffer, buf_size);

	rc = EVP_VerifyFinal(&ectx, (unsigned char *) signature, 
		sig_size, (EVP_PKEY *) key);
	if (!rc)
		rc = SLURM_ERROR;
	else
		rc = SLURM_SUCCESS;

#ifdef HAVE_EVP_MD_CTX_CLEANUP
	/* Note: Likely memory leak if this function is absent */
	EVP_MD_CTX_cleanup(&ectx);
#endif

	return rc;
}
