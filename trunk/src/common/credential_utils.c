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

typedef struct revoked_credential
{
	int job_id ;
	time_t expiration ;
} revoked_credential_t ;

int clear_expired_revoked_credentials ( List list ) ;
int verify_credential ( slurm_ssl_key_ctx_t * verify_ctx , slurm_job_credential_t * credential )
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

	if ( is_credential_still_valid ( credential , revoked_credentials_list_t * list ) )
	{
		slurm_seterrno ( ESLURMD_CREDENTIAL_REVOKED ) ;
		error_code = SLURM_ERROR ;
		goto return_ ;
	}
	*/

	return_:
	return error_code ;
}

int insert_expired_credential ( slurm_job_credential_t * credential , List list ) 
{
	return SLURM_SUCCESS ;
}

int is_credential_still_valid ( slurm_job_credential_t * credential , List list )
{
	ListIterator iterator ;
	iterator = list_iterator_create( list ) ;
	clear_expired_revoked_credentials ( list ) ;
	/*revoked_credential_t * revoked_credential ;
	while ( ( revoked_credential = list_next ( iterator ) ) )
	{
		if ( credential -> job_id == revoked_credential -> job_id ) ;
	}
	*/
	return SLURM_SUCCESS ;
}

int clear_expired_revoked_credentials ( List list )
{
	time_t now = time ( NULL ) ;
	ListIterator iterator ;
	iterator = list_iterator_create( list ) ;
	/*revoked_credential_t * revoked_credential ;
	while ( ( revoked_credential = list_next ( iterator ) ) )
	{
		if ( now + EXPIATION_WINDOW > revoked_credential -> expiration )
		{
			list_delete ( iterator ) ;
		}
	}
	*/
	return SLURM_SUCCESS ;
}
	
