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

/* slurm_ssl_init
 * calls the approriate ssl init functions for crypto functions 
 * should be called once before using other slurm_ssl functions
 * RET 			- return code
 */
int slurm_ssl_init ( ) ;

/* slurm_ssl_destroy
 * calls the approriate ssl destroy fucntions for crypto functions
 * should be called right before exit
 * RET 			- return code
 */ 
int slurm_ssl_destroy ( ) ;

/* slurm_init_signer
 * loads a private key to later be used to sign messages
 * OUT ctx		- context to initialize
 * IN path		- path to private key
 * RET			- return code
 */
int slurm_init_signer ( slurm_ssl_key_ctx_t * ctx , char * path ) ;

/* slurm_init_verifier
 * loads a public key out of a X509 cert to verify signed messages
 * OUT ctx		- context to initialize
 * IN path		- path to certificate 
 * RET			- return code
 */
int slurm_init_verifier ( slurm_ssl_key_ctx_t * ctx , char * path ) ;


/* slurm_destroy_ssl_key_ctx
 * destroys an initialezed ssl_key_ctx
 * IN ctx		- context to destroy 
 * RET			- return code
 */
int slurm_destroy_ssl_key_ctx ( slurm_ssl_key_ctx_t * ctx ) ;

/* slurm_ssl_sign
 * using a private key ctx and a buffer creates a signature in signature_buffer
 * IN ctx		- private key ctx
 * IN data_buffer	- buffer to sign
 * IN data_length	- length of data buffer
 * OUT signature_buffer	- signature
 * OUT signature_length	- signature length
 * RET 			- return code
 */
int slurm_ssl_sign ( slurm_ssl_key_ctx_t * ctx , char * data_buffer , int data_length , char * signature_buffer , int * signature_length ) ;

/* slurm_ssl_verify
 * using a public key ctx and a buffer verifies a signature in signature_buffer
 * IN ctx		- private key ctx
 * IN data_buffer	- buffer to verify 
 * IN data_length	- length of data buffer
 * OUT signature_buffer	- signature
 * OUT signature_length	- signature length
 * RET 			- return code
 */
int slurm_ssl_verify ( slurm_ssl_key_ctx_t * ctx , char * data_buffer , int data_length , char * signature_buffer , int signature_length ) ;

#endif
