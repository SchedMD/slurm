#ifndef _CREDENTIAL_UTILS_H
#define _CREDENTIAL_UTILS_H
#include <stdio.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

#include <src/common/slurm_protocol_api.h>

enum key_type { SIGNER_PRIVATE_KEY , VERIFIER_PUBLIC_KEY } ;
enum { SLURM_OPENSSL_SIGNED = 1 } ;
enum { SLURM_OPENSSL_VERIFIED = 1 } ;

typedef struct credential_tools_ctx
{
	enum key_type ;
	unsigned int key_length ;
	union key
	{
		EVP_PKEY * private ;
		EVP_PKEY * public ;
	} key ;
} credential_tools_ctx_t ;
#endif
