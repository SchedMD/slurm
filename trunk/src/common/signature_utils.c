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
	};

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
	if (ctx) 
		EVP_PKEY_free(ctx->key.private);
	return SLURM_SUCCESS;
}


int
slurm_ssl_sign(slurm_ssl_key_ctx_t *ctx, char *data_buffer,
	       int data_length, char *signature_buffer,
	       int *signature_length)
{
	int rc;
	EVP_MD_CTX md_ctx;
	if (EVP_PKEY_size(ctx->key.private) > SLURM_SSL_SIGNATURE_LENGTH) 
		slurm_seterrno_ret(ESLURMD_SIGNATURE_FIELD_TOO_SMALL);

	EVP_SignInit(&md_ctx, EVP_sha1());
	EVP_SignUpdate(&md_ctx, data_buffer, data_length);
	if ((rc = 
	    EVP_SignFinal(&md_ctx, signature_buffer, signature_length,
			   ctx->key.private)) != SLURM_OPENSSL_SIGNED) {
		ERR_print_errors_fp(log_fp()); 
		slurm_seterrno(ESLURMD_OPENSSL_ERROR);
		return SLURM_ERROR;
	}
	info("signature length = %d", *signature_length);
	return SLURM_SUCCESS;
}

int
slurm_ssl_verify(slurm_ssl_key_ctx_t * ctx, char *data_buffer,
		 int data_length, char *signature_buffer,
		 int signature_length)
{
	int rc;
	EVP_MD_CTX md_ctx;

	EVP_VerifyInit(&md_ctx, EVP_sha1());
	EVP_VerifyUpdate(&md_ctx, data_buffer, data_length);
	if ((rc =
	     EVP_VerifyFinal(&md_ctx, signature_buffer, signature_length,
			     ctx->key.public)) != SLURM_OPENSSL_VERIFIED) {
		error("EVP_VerifyFinal: %s", 
		      ERR_error_string(ERR_get_error(), NULL));
		slurm_seterrno_ret(ESLURMD_OPENSSL_ERROR);
	}
	return SLURM_SUCCESS;
}
