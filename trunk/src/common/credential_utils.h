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
#include <src/common/signature_utils.h>

typedef struct credential_state {
	int job_id;		/* job_id that this credential cooresponds to */
	short int revoked;	/* boolean true/false */
	short int procs_allocated;	/* number of credential procs running */
	short int total_procs;	/* number of procs in credential */
	time_t revoke_time;	/* time of revoke - this is informational only not used */
	time_t expiration;	/* expiration date set at credential creation time */
} credential_state_t;

/* time to wait after expiration_time before removing credential_state from credential_state_list*/
#define EXPIRATION_WINDOW 600

/* function prototypes */
/* initialize_credential_state_list
 * called from slurmd_init initializes the List structure pointed to by list
 * IN list	- type List 
 */
int initialize_credential_state_list(List * list);

/* destroy_credential_state_list
 * destroys a initialized list
 * IN list	- type List 
 */
int destroy_credential_state_list(List list);

/* verify_credential
 * given a credential message and a verify_ctx containing the public key
 * this method verifies the credential and creates the necessary state object in the 
 * credential_state_list
 * IN verfify_ctx		- slurm ssl public key ctx
 * IN credential		- credential to verify
 * IN credential_state_list	- list to add credential state object to 
 */
int verify_credential(slurm_ssl_key_ctx_t * verfify_ctx,
		      slurm_job_credential_t * credential,
		      List credential_state_list);

/* sign_credential
 * signs a credential before transmit
 * used by slurmctld
 * IN sign_ctx			- slurm ssl private key ctx
 * IN credential		- credential to sign 
 */
int sign_credential(slurm_ssl_key_ctx_t * sign_ctx,
		    slurm_job_credential_t * credential);

/* revoke_credential
 * expires a credential in the credential_state_list
 * IN revoke_msg		- revoke rpc message
 * IN list			- list to revoke credential state object in
 */
int revoke_credential(revoke_credential_msg_t * revoke_msg, List list);

#endif
