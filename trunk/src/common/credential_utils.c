#include <stdio.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

#include <src/common/log.h>
#include <src/common/xmalloc.h>
#include <src/common/slurm_errno.h>
#include <src/common/slurm_protocol_errno.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_protocol_pack.h>
#include <src/slurmd/credential_utils.h>
#include <src/slurmd/signature_utils.h>
#include <src/common/hostlist.h>
#define MAX_NAME_LEN 1024

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
	
        if ( ( error_code = getnodename (this_node_name, MAX_NAME_LEN) ) )
		                fatal ("slurmd: errno %d from getnodename", errno);
	
	/*
	if ( verify_node_name_list ( this_node_name , credential -> node_list ) ) 
	{
		slurm_seterrno ( ESLURMD_CREDENTIAL_EXPIRED ) ;
		error_code = SLURM_ERROR ;
		goto return_ ;

	}

	if ( is_credential_still_valid ( credential -> job_id ) )
	{
		slurm_seterrno ( ESLURMD_CREDENTIAL_REVOKED ) ;
		error_code = SLURM_ERROR ;
		goto return_ ;
	}
	*/

	return_:
	return error_code ;
}
