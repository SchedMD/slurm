#include <stdio.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

#include <src/common/log.h>
#include <src/common/list.h>
#include <src/common/xmalloc.h>
#include <src/common/slurm_errno.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_protocol_pack.h>
#include <src/common/credential_utils.h>
#include <src/common/signature_utils.h>
#include <src/common/hostlist.h>
#define MAX_NAME_LEN 1024

/* global variables */

/* prototypes */

static int clear_expired_revoked_credentials(List list);
static int is_credential_still_valid(slurm_job_credential_t * credential,
				     List list);


static int init_credential_state(credential_state_t *state, 
		                 slurm_job_credential_t *cred);
void free_credential_state(void *credential_state);
static int insert_credential_state(slurm_job_credential_t * credential,
				   List list);
static int insert_revoked_credential_state(revoke_credential_msg_t *
					   revoke_msg, List list);

int
sign_credential(slurm_ssl_key_ctx_t *ctx, slurm_job_credential_t *cred)
{
	char buf[4096];
	char *bufp  = buf;
	int bufsz   = sizeof(buf);
	int size    = bufsz;
	int sigsize = SLURM_SSL_SIGNATURE_LENGTH;
	int length;

	pack_job_credential(cred, (void **)&bufp, &size);
	length = bufsz - size - SLURM_SSL_SIGNATURE_LENGTH;

	if (slurm_ssl_sign(ctx, buf, length, cred->signature, &sigsize)) 
		slurm_seterrno_ret(ESLURMD_ERROR_SIGNING_CREDENTIAL);

	if (sigsize != SLURM_SSL_SIGNATURE_LENGTH)
		error("signature size not correct in ssl_sign!");

	return SLURM_SUCCESS;
}


int
verify_credential(slurm_ssl_key_ctx_t *ctx, slurm_job_credential_t *cred,
		  List cred_state_list)
{
	char buf[4096];
	char *bufp     = buf;
	int bufsz      = sizeof(buf);
	int size       = bufsz;
	int rc         = SLURM_SUCCESS;
	time_t now     = time(NULL);
	char this_node_name[MAX_NAME_LEN];
	int length;

	pack_job_credential(cred, (void **)&bufp, &size);
	length = bufsz - size - SLURM_SSL_SIGNATURE_LENGTH;

	if (slurm_ssl_verify(ctx, buf, length, cred->signature, 
	                     SLURM_SSL_SIGNATURE_LENGTH)) 
		slurm_seterrno_ret(ESLURMD_INVALID_JOB_CREDENTIAL);

	if (cred->expiration_time - now < 0) 
		slurm_seterrno_ret(ESLURMD_NODE_NAME_NOT_PRESENT_IN_CREDENTIAL);

	if ((rc = getnodename(this_node_name, MAX_NAME_LEN)))
		fatal("slurmd: getnodename: %m");

	/* XXX: Fix this I suppose?
	   if ( verify_node_name_list ( this_node_name , credential -> node_list ) ) 
	   {
	   slurm_seterrno ( ESLURMD_CREDENTIAL_EXPIRED ) ;
	   error_code = SLURM_ERROR ;
	   goto return_ ;

	   }
	 */

	/* XXX:
	 * need code to check to make sure that only the specified 
	 * number of procs per node are used to launch tasks and not more
	 */

	if (is_credential_still_valid(cred, cred_state_list)) 
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

int revoke_credential(revoke_credential_msg_t *msg, List list)
{
	time_t now = time(NULL);
	ListIterator iterator;
	credential_state_t *credential_state;

	iterator = list_iterator_create(list);

	while ((credential_state = list_next(iterator))) {
		if (msg->job_id == credential_state->job_id) {
			credential_state->revoked = true;
			credential_state->revoke_time = now;
			return SLURM_SUCCESS;
		}
	}
	insert_revoked_credential_state(msg, list);
	return SLURM_SUCCESS;
}

int
is_credential_still_valid(slurm_job_credential_t * credential, List list)
{
	ListIterator iterator;
	credential_state_t *credential_state;

	clear_expired_revoked_credentials(list);

	iterator = list_iterator_create(list);

	while ((credential_state = list_next(iterator))) {
		if (credential->job_id == credential_state->job_id) {
			if (credential_state->revoked) 
				return ESLURMD_CREDENTIAL_REVOKED;
			/* only allows one launch this is a problem but 
			 * othrewise we have to do accounting 
			 * of how many proccess are running and how many 
			 * the credential allows. */

			credential_state->revoked = true;

			/* credential_state and is good */

			return SLURM_SUCCESS;
		}
	}
	/* credential_state does not exist */
	insert_credential_state(credential, list);

	return SLURM_SUCCESS;
}

int clear_expired_revoked_credentials(List list)
{
	time_t now = time(NULL);
	ListIterator iterator;
	credential_state_t *credential_state;

	iterator = list_iterator_create(list);
	while ((credential_state = list_next(iterator))) {
		if (now + EXPIRATION_WINDOW > credential_state->expiration) 
			list_delete(iterator);
	}
	return SLURM_SUCCESS;
}

int initialize_credential_state_list(List * list)
{
	*list = list_create(free_credential_state);
	return SLURM_SUCCESS;
}

int destroy_credential_state_list(List list)
{
	list_destroy(list);
	return SLURM_SUCCESS;
}

int
init_credential_state(credential_state_t * credential_state,
		      slurm_job_credential_t * credential)
{
	credential_state->job_id = credential->job_id;
	credential_state->expiration = credential->expiration_time;
	credential_state->revoked = false;
	return SLURM_SUCCESS;
}

void free_credential_state(void *credential_state)
{
	if (credential_state) {
		xfree(credential_state);
	}
}

int insert_credential_state(slurm_job_credential_t * credential, List list)
{
	credential_state_t *credential_state;
	credential_state = xmalloc(sizeof(slurm_job_credential_t));
	init_credential_state(credential_state, credential);
	list_append(list, credential_state);
	return SLURM_SUCCESS;
}

int
insert_revoked_credential_state(revoke_credential_msg_t * revoke_msg, List list)
{
	time_t now = time(NULL);
	credential_state_t *credential_state;

	credential_state = xmalloc(sizeof(slurm_job_credential_t));
	credential_state->job_id = revoke_msg->job_id;
	credential_state->expiration = revoke_msg->expiration_time;
	credential_state->revoked = true;
	credential_state->revoke_time = now;
	list_append(list, credential_state);
	return SLURM_SUCCESS;
}
