/*****************************************************************************\
 * signature_utils.c - functions related to job cred signatures
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tewk@llnl.gov>.
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
#endif

#include <stdio.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

#include <slurm/slurm_errno.h>

#include "src/common/credential_utils.h"
#include "src/common/log.h"
#include "src/common/signature_utils.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"


int slurm_ssl_init()
{
	ERR_load_crypto_strings();
	OpenSSL_add_all_algorithms();
	return SLURM_SUCCESS;
}

int slurm_ssl_destroy()
{
	EVP_cleanup();
	ERR_free_strings();
	return SLURM_SUCCESS;
}

int slurm_init_signer(slurm_ssl_key_ctx_t * ctx, char *path)
{
	FILE *fp;

	if (!(fp = fopen(path, "r"))) {
		error ("can't open key file '%s' : %m", path);
		return SLURM_ERROR;
	};

	ctx->key.private = NULL;
	if (!PEM_read_PrivateKey(fp, &ctx->key.private, NULL, NULL)) {
		error ("PEM_read_PrivateKey [%s]: %m", path);
		slurm_seterrno_ret(ESLURMD_OPENSSL_ERROR);
	}
	fclose(fp);

	return SLURM_SUCCESS;
}

int slurm_init_verifier(slurm_ssl_key_ctx_t * ctx, char *path)
{
	FILE *fp = NULL;

	if ((fp = fopen(path, "r")) == NULL) {
		error ("can't open certificate file '%s' : %m ", path);
		return SLURM_ERROR;
	}

	ctx->key.public = NULL;
	if (!PEM_read_PUBKEY(fp, &ctx->key.public, NULL, NULL)) {
		error("PEM_read_PUBKEY[%s]: %m",path);
		slurm_seterrno_ret(ESLURMD_OPENSSL_ERROR);
	}
	fclose(fp);

	return SLURM_SUCCESS;
}

int slurm_destroy_ssl_key_ctx(slurm_ssl_key_ctx_t * ctx)
{
	if (ctx) EVP_PKEY_free(ctx->key.private);
	return SLURM_SUCCESS;
}


int
slurm_ssl_sign(slurm_ssl_key_ctx_t *ctx, 
               char *data, int datalen, char *sig,  int *siglen )
{
	EVP_MD_CTX ectx;

	if (EVP_PKEY_size(ctx->key.private) > SLURM_SSL_SIGNATURE_LENGTH) 
		slurm_seterrno_ret(ESLURMD_SIGNATURE_FIELD_TOO_SMALL);

	EVP_SignInit(&ectx, EVP_sha1());

	EVP_SignUpdate(&ectx, data, datalen);

	if (!EVP_SignFinal(&ectx, sig, siglen, ctx->key.private)) {
		ERR_print_errors_fp(log_fp()); 
		slurm_seterrno_ret(ESLURMD_OPENSSL_ERROR);
	}

	return SLURM_SUCCESS;
}

int
slurm_ssl_verify(slurm_ssl_key_ctx_t * ctx, 
                 char *data, int datalen, char *sig, int siglen)
{
	EVP_MD_CTX ectx;

	EVP_VerifyInit(&ectx, EVP_sha1());

	EVP_VerifyUpdate(&ectx, data, datalen);

	if (!EVP_VerifyFinal(&ectx, sig, siglen, ctx->key.public)) {
		error("EVP_VerifyFinal: %s", 
		      ERR_error_string(ERR_get_error(), NULL));
		slurm_seterrno_ret(ESLURMD_OPENSSL_ERROR);
	}
	return SLURM_SUCCESS;
}
