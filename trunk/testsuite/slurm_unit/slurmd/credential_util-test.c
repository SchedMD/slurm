#include <string.h> 

#include <src/common/log.h>
#include <src/slurmd/credential_utils.h>

int main ( int argc , char * argv[] ) 
{
	int signature_length ; 
	char data [] = "This is test data to sign and verify" ;
	char data2 [] = "This is test data to sign and verify" ;
	char signature [4096]  ;
	
	slurm_ssl_key_ctx_t signer_ctx ;
	slurm_ssl_key_ctx_t verifier_ctx ;

	slurm_ssl_init ( ) ;

	slurm_init_signer ( & signer_ctx , "newreq.pem" ) ;
	slurm_init_verifier ( & verifier_ctx , "newcert.pem" ) ;

	if ( slurm_ssl_sign ( & signer_ctx , data , strlen ( data ) , signature , & signature_length ) ) ;
		info ( " sign succeeded %i", signature_length ) ;

	if ( slurm_ssl_verify ( & verifier_ctx , data2 , strlen ( data2 ) , signature , signature_length ) ) ;
		info ( " verify succeeded " ) ;
	

	slurm_destroy_ssl_key_ctx ( & signer_ctx ) ;
	slurm_destroy_ssl_key_ctx ( & verifier_ctx ) ;

	slurm_ssl_destroy ( ) ;
}
