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
#include <src/common/slurm_protocol_errno.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_protocol_pack.h>
#include <src/slurmd/credential_utils.h>
#include <src/slurmd/signature_utils.h>
#include <src/common/hostlist.h>
#define MAX_NAME_LEN 1024

/* global variables */

/* prototypes */

static int clear_expired_revoked_credentials ( List list ) ;
static int is_credential_still_valid ( slurm_job_credential_t * credential , List list ) ;
	
	
static int init_credential_state ( credential_state_t * credential_state , slurm_job_credential_t * credential ) ;
void free_credential_state ( void * credential_state ) ;
static int insert_credential_state ( slurm_job_credential_t * credential , List list ) ;
	
int sign_credential ( slurm_ssl_key_ctx_t * sign_ctx , slurm_job_credential_t * credential ) 
{
	char buffer [4096] ;
	char * buf_ptr = buffer ;
	int buf_size = sizeof ( buffer ) ;
	int size = sizeof ( buffer ) ;
	int error_code = SLURM_SUCCESS ;
	int signature_size ;
	
	pack_job_credential ( credential , (void ** ) & buf_ptr , & size ) ;
	if ( slurm_ssl_sign ( sign_ctx , buffer , buf_size - size - SLURM_SSL_SIGNATURE_LENGTH , credential -> signature , & signature_size ) )
	{
		slurm_seterrno ( ESLURMD_ERROR_SIGNING_CREDENTIAL ) ;
		error_code = SLURM_ERROR ;
		goto return_ ;
	}

	return_:
	return error_code ;
}

int verify_credential ( slurm_ssl_key_ctx_t * verify_ctx , slurm_job_credential_t * credential , List credential_state_list ) 
{
	char buffer [4096] ;
	char * buf_ptr = buffer ;
	int buf_size = sizeof ( buffer ) ;
	int size = sizeof ( buffer ) ;
	int error_code = SLURM_SUCCESS ;
	time_t now = time ( NULL ) ;
	char this_node_name[MAX_NAME_LEN] ;
	
	pack_job_credential ( credential , (void ** ) & buf_ptr , & size ) ;
	if ( slurm_ssl_verify ( verify_ctx , buffer , buf_size - size - SLURM_SSL_SIGNATURE_LENGTH , credential -> signature , SLURM_SSL_SIGNATURE_LENGTH ) )
	{
		slurm_seterrno ( ESLURMD_INVALID_JOB_CREDENTIAL ) ;
		error_code = SLURM_ERROR ;
		goto return_ ;
	}

	if ( credential -> expiration_time - now < 0 )
	{
		slurm_seterrno ( ESLURMD_NODE_NAME_NOT_PRESENT_IN_CREDENTIAL ) ;
		error_code = SLURM_ERROR ;
		goto return_ ;
	}

	if ( ( error_code = getnodename ( this_node_name, MAX_NAME_LEN ) ) )
		fatal ("slurmd: failed to get hostname %m from getnodename");

	/*
	if ( verify_node_name_list ( this_node_name , credential -> node_list ) ) 
	{
		slurm_seterrno ( ESLURMD_CREDENTIAL_EXPIRED ) ;
		error_code = SLURM_ERROR ;
		goto return_ ;

	}
	*/
	
	/*
	 * need code to check to make sure that only the specified number of procs per node
	 * are used to launch tasks and not more
	 */

	if ( is_credential_still_valid ( credential , credential_state_list ) )
	{
		error_code = SLURM_ERROR ;
		goto return_ ;
	}

	return_:
	return error_code ;
}

int revoke_credential ( revoke_credential_msg_t * revoke_msg , List list ) 
{
	time_t now = time ( NULL ) ;
	ListIterator iterator ;
	credential_state_t * credential_state ;
	
	iterator = list_iterator_create( list ) ;

	while ( ( credential_state = list_next ( iterator ) ) )
	{
		if ( revoke_msg -> job_id == credential_state -> job_id ) ;
		{
			credential_state -> revoked = true ;
			credential_state -> revoke_time = now ;
			return SLURM_SUCCESS ;
		}
	}
	slurm_seterrno ( ESLURMD_CREDENTIAL_TO_EXPIRE_DOESNOT_EXIST ) ;
	return SLURM_FAILURE ;
}

int is_credential_still_valid ( slurm_job_credential_t * credential , List list )
{
	ListIterator iterator ;
	credential_state_t * credential_state ;
	
	iterator = list_iterator_create( list ) ;
	clear_expired_revoked_credentials ( list ) ;

	while ( ( credential_state = list_next ( iterator ) ) )
	{
		if ( credential -> job_id == credential_state -> job_id ) ;
		{
			if ( credential_state -> revoked )
			{
				return ESLURMD_CREDENTIAL_REVOKED ;
			}
			/* only allows one launch this is a problem but othrewise we have to do accounting 
			 * of how many proccess are running and how many the credential allows. */
			credential_state -> revoked = true ;
			/* credential_state and is good */
			return SLURM_SUCCESS ;
		}
	}
	/* credential_state does not exist */
	insert_credential_state ( credential , list ) ;	

	return SLURM_SUCCESS ;
}

int clear_expired_revoked_credentials ( List list )
{
	time_t now = time ( NULL ) ;
	ListIterator iterator ;
	credential_state_t * credential_state ;

	iterator = list_iterator_create( list ) ;
	while ( ( credential_state = list_next ( iterator ) ) )
	{
		if ( now + EXPIRATION_WINDOW > credential_state -> expiration )
		{
			list_delete ( iterator ) ;
		}
	}
	return SLURM_SUCCESS ;
}

int initialize_credential_state_list ( List * list )
{
	*list = list_create ( free_credential_state ) ;
	return SLURM_SUCCESS ;
}

int destroy_credential_state_list ( List list )
{
	list_destroy ( list ) ;
	return SLURM_SUCCESS ;
}

int init_credential_state ( credential_state_t * credential_state , slurm_job_credential_t * credential )
{
	credential_state -> job_id = credential -> job_id ;	
	credential_state -> expiration = credential -> expiration_time  ;
	credential_state -> revoked = false ;
	return SLURM_SUCCESS ;
}

void free_credential_state ( void * credential_state )
{
	if ( credential_state )
	{
		xfree ( credential_state ) ;
	}
}

int insert_credential_state ( slurm_job_credential_t * credential , List list )
{
	credential_state_t * credential_state ;
	credential_state = xmalloc ( sizeof ( slurm_job_credential_t ) ) ;
	init_credential_state ( credential_state , credential ) ;
	list_append ( list , credential_state ) ;
	return SLURM_SUCCESS ;	
}
