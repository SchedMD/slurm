#include <string.h> 

#include <src/slurmd/credential_utils.h>

int main ( int argc , char * argv[] ) 
{
	int signature_length ; 
	char data [] = "This is test data to sign and verify" ;
	char signature [20]  ;
	
	credential_tools_ctx_t signer_ctx ;
	credential_tools_ctx_t verifier_ctx ;

	ssl_init ( ) ;

	init_signer ( & signer_ctx , "newreq.pem" ) ;
	init_verifier ( & verifier_ctx , "newcert.pem" ) ;

	if ( credential_sign ( & signer_ctx , data , strlen ( data ) , signature , & signature_length ) ) ;
		info ( " sign succeeded %i", signature_length ) ;

	if ( credential_verify ( & verifier_ctx , data , strlen ( data ) , signature , signature_length ) ) ;
		info ( " verify succeeded " ) ;
	

	destroy_credential_ctx ( & signer_ctx ) ;
	destroy_credential_ctx ( & verifier_ctx ) ;

	ssl_destroy ( ) ;
}
