#ifndef _SIGNATURE_UTILS_H
#define _SIGNATURE_UTILS_H
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

typedef struct slurm_ssl_key_ctx
{
	enum key_type ;
	unsigned int key_length ;
	union key
	{
		EVP_PKEY * private ;
		EVP_PKEY * public ;
	} key ;
} slurm_ssl_key_ctx_t ;

int slurm_ssl_init ( ) ;
int slurm_ssl_destroy ( ) ;

int slurm_init_signer ( slurm_ssl_key_ctx_t * ctx , char * path ) ;
int slurm_init_verifier ( slurm_ssl_key_ctx_t * ctx , char * path ) ;

int slurm_destroy_ssl_key_ctx ( slurm_ssl_key_ctx_t * ctx ) ;

int slurm_ssl_sign ( slurm_ssl_key_ctx_t * ctx , char * data_buffer , int data_length , char * signature_buffer , int * signature_length ) ;
int slurm_ssl_verify ( slurm_ssl_key_ctx_t * ctx , char * data_buffer , int data_length , char * signature_buffer , int signature_length ) ;

#endif
