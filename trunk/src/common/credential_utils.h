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

#include <src/common/list.h>
#include <src/common/slurm_protocol_api.h>
#include <src/slurmd/signature_utils.h>

typedef struct credential_state
{
	int job_id ;
	short int revoked ;
	short int procs ;
	short int totol_procs ;
	short int procs_allocated ;
	time_t revoke_time ;
	time_t expiration ;
} credential_state_t ;

#define EXPIRATION_WINDOW 600

/* function prototypes */
int initialize_credential_state_list ( List * list ) ;
int destroy_credential_state_list ( List list ) ;
int verify_credential ( slurm_ssl_key_ctx_t * verfify_ctx , slurm_job_credential_t * credential , List credential_state_list ) ;
int sign_credential ( slurm_ssl_key_ctx_t * sign_ctx , slurm_job_credential_t * credential ) ; 
int expire_credential ( revoke_credential_msg_t * revoke_msg , List list ) ;

#endif
