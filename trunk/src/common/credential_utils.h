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

#include <src/common/slurm_protocol_api.h>
#include <src/slurmd/signature_utils.h>
int verify_credential ( slurm_ssl_key_ctx_t * verfify_ctx , slurm_job_credential_t * credential ) ;

#endif
