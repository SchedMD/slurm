/*****************************************************************************\
 *  credential_utils.c - slurm authentication credential management functions
 *****************************************************************************
 *  Written by Jay Windley <jwindley@lnxi.com>, et. al.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

#include "src/common/credential_utils.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/signature_utils.h"
#include "src/common/slurm_errno.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"

#define MAX_NAME_LEN 1024

/* global variables */

/* prototypes */

static int  _clear_expired_revoked_credentials(List list);
static int  _is_credential_still_valid(slurm_job_credential_t * credential,
				       List list);
static void _free_credential_state(void *credential_state);
static int  _init_credential_state(credential_state_t * state,
				   slurm_job_credential_t * cred);
static int  _insert_credential_state(slurm_job_credential_t * credential,
				     List list);
static int  _insert_revoked_credential_state(revoke_credential_msg_t *
					     revoke_msg, List list);
static void _pack_one_cred(credential_state_t *credential_state_ptr, 
			   Buf buffer);
static int  _unpack_one_cred(credential_state_t *credential_state_ptr, 
			     Buf buffer);


int
sign_credential(slurm_ssl_key_ctx_t * ctx, slurm_job_credential_t * cred)
{
	Buf buffer;
	int length, rc;
	int sigsize = SLURM_SSL_SIGNATURE_LENGTH;

	buffer = init_buf(4096);
	pack_job_credential(cred, buffer);
	length = get_buf_offset(buffer) - SLURM_SSL_SIGNATURE_LENGTH;

	rc = slurm_ssl_sign(ctx, get_buf_data(buffer), length,
			    cred->signature, &sigsize);
	free_buf(buffer);

	if (rc)
		slurm_seterrno_ret(ESLURMD_ERROR_SIGNING_CREDENTIAL);

	if (sigsize != SLURM_SSL_SIGNATURE_LENGTH)
		error("signature size not correct in ssl_sign!");

	return SLURM_SUCCESS;
}


int
verify_credential(slurm_ssl_key_ctx_t * ctx, slurm_job_credential_t * cred,
		  List cred_state_list)
{
	int rc;
	time_t now = time(NULL);
	int length;
	Buf buffer;

	buffer = init_buf(4096);
	pack_job_credential(cred, buffer);
	length = get_buf_offset(buffer) - SLURM_SSL_SIGNATURE_LENGTH;

	rc = slurm_ssl_verify(ctx, get_buf_data(buffer), length,
			      cred->signature, SLURM_SSL_SIGNATURE_LENGTH);
	free_buf(buffer);

	if (rc) {
		error("Invalid credential submitted");
		slurm_seterrno_ret(ESLURMD_INVALID_JOB_CREDENTIAL);
	}

	if (cred->expiration_time < now) {
		error("credential has expired expiration=%lx now=%lx",
		      (long)cred->expiration_time , (long)now);
		slurm_seterrno_ret(ESLURMD_CREDENTIAL_EXPIRED);
	}

#if WE_WANT_TO_CONFIRM_NODELIST_IN_CREDENTIAL
	/* FIXME:XXX: if so desired */
	char this_node_name[MAX_NAME_LEN];
	if ((rc = getnodename(this_node_name, MAX_NAME_LEN)))
		fatal("slurmd: getnodename: %m");

	if ( verify_node_name_list ( this_node_name , 
	                             credential->node_list ) )
		slurm_seterrno_ret(
			ESLURMD_NODE_NAME_NOT_PRESENT_IN_CREDENTIAL);
#endif

	/* XXX:
	 * need code to check to make sure that only the specified 
	 * number of procs per node are used to launch tasks and not more
	 */

	if ((rc = _is_credential_still_valid(cred, cred_state_list))) {
		slurm_seterrno_ret(rc);
	}

	return SLURM_SUCCESS;
}

void print_credential(slurm_job_credential_t * cred)
{
	int i, j = 0;
	long long_tmp;
	char sig_str[SLURM_SSL_SIGNATURE_LENGTH*4];

	for (i=0; i<SLURM_SSL_SIGNATURE_LENGTH; i+=sizeof(long)) {
		memcpy(&long_tmp, &cred->signature[i], sizeof(long));
		sprintf(&sig_str[(j++)*9], "%8lx ", long_tmp);
	}

       info("cred uid:%u job_id:%u time:%lx",
            cred->user_id, cred->job_id, (long)cred->expiration_time);
       info("cred signature:%s", sig_str);
}

int revoke_credential(revoke_credential_msg_t * msg, List list)
{
	time_t now = time(NULL);
	ListIterator iterator;
	credential_state_t *credential_state;

	iterator = list_iterator_create(list);

	while ((credential_state = list_next(iterator))) {
		if (msg->job_id == credential_state->job_id) {
			credential_state->revoked     = true;
			credential_state->revoke_time = now;
			list_iterator_destroy(iterator);
			return SLURM_SUCCESS;
		}
	}
	_insert_revoked_credential_state(msg, list);
	list_iterator_destroy(iterator);
	return SLURM_SUCCESS;
}

int
_is_credential_still_valid(slurm_job_credential_t * credential, List list)
{
	ListIterator iterator;
	credential_state_t *credential_state;

	_clear_expired_revoked_credentials(list);

	iterator = list_iterator_create(list);

	while ((credential_state = list_next(iterator))) {
		if (credential->job_id == credential_state->job_id) {
			list_iterator_destroy(iterator);
			if (credential_state->revoked)
				return ESLURMD_CREDENTIAL_REVOKED;
			/* only allows one launch this is a problem but 
			 * otherwise we have to do accounting 
			 * of how many proccess are running and how many 
			 * the credential allows. */

			credential_state->revoked = true;

			/* credential_state and is good */
			return SLURM_SUCCESS;
		}
	}
	/* credential_state does not exist */
	_insert_credential_state(credential, list);

	list_iterator_destroy(iterator);
	return SLURM_SUCCESS;
}

int _clear_expired_revoked_credentials(List list)
{
	time_t now = time(NULL);
	ListIterator iterator;
	credential_state_t *credential_state;

	iterator = list_iterator_create(list);
	while ((credential_state = list_next(iterator))) {
		if (now + EXPIRATION_WINDOW > credential_state->expiration)
			list_delete(iterator);
	}
	list_iterator_destroy(iterator);
	return SLURM_SUCCESS;
}

int initialize_credential_state_list(List * list)
{
	*list = list_create(_free_credential_state);
	return SLURM_SUCCESS;
}

int destroy_credential_state_list(List list)
{
	list_destroy(list);
	return SLURM_SUCCESS;
}

int
_init_credential_state(credential_state_t * credential_state,
		      slurm_job_credential_t * credential)
{
	credential_state->job_id	= credential->job_id;
	credential_state->expiration	= credential->expiration_time;
	credential_state->revoked	= false;
	return SLURM_SUCCESS;
}

void _free_credential_state(void *credential_state)
{
	if (credential_state) {
		xfree(credential_state);
	}
}

int _insert_credential_state(slurm_job_credential_t * credential, List list)
{
	credential_state_t *credential_state;
	credential_state = xmalloc(sizeof(slurm_job_credential_t));
	_init_credential_state(credential_state, credential);
	list_append(list, credential_state);
	return SLURM_SUCCESS;
}

int
_insert_revoked_credential_state(revoke_credential_msg_t * revoke_msg,
				List list)
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

/* pack_credential_list
 * pack a list of credentials into a machine independent format buffer
 * IN list		- list to credentials to pack
 * IN/OUT buffer	- existing buffer into which the credential
 *			  information should be stored
 */ 
void pack_credential_list(List list, Buf buffer)
{
	ListIterator iterator;
	credential_state_t *credential_state_ptr;

	iterator = list_iterator_create(list);
	while ((credential_state_ptr = list_next(iterator)))
		_pack_one_cred(credential_state_ptr, buffer);
	list_iterator_destroy(iterator);
}

/* unpack_credential_list
 * unpack a list of credentials from a machine independent format buffer
 * IN/OUT list		- existing list onto which the records in 
 *			  the buffer are added
 * IN buffer		- existing buffer from which the credential
 *			  information should be read
 * RET int		- zero or error code
 */ 
int unpack_credential_list(List list, Buf buffer)
{
	credential_state_t *credential_state_ptr;

	while (1) {
		credential_state_ptr = xmalloc(sizeof(slurm_job_credential_t));
		if (_unpack_one_cred(credential_state_ptr, buffer)) {
			xfree(credential_state_ptr);
			return SLURM_ERROR;
		} else	
			list_append(list, credential_state_ptr);
	}
	return SLURM_SUCCESS;
}

static void 
_pack_one_cred(credential_state_t *credential_state_ptr, Buf buffer)
{
	pack32(credential_state_ptr->job_id,		buffer);
	pack16(credential_state_ptr->revoked,		buffer);
	pack16(credential_state_ptr->procs_allocated,	buffer);
	pack16(credential_state_ptr->total_procs,	buffer);
	pack_time(credential_state_ptr->revoke_time,	buffer);
	pack_time(credential_state_ptr->expiration,	buffer);
}

static int 
_unpack_one_cred(credential_state_t *credential_state_ptr, Buf buffer)
{
	safe_unpack32(&credential_state_ptr->job_id,		buffer);
	safe_unpack16(&credential_state_ptr->revoked,		buffer);
	safe_unpack16(&credential_state_ptr->procs_allocated,	buffer);
	safe_unpack16(&credential_state_ptr->total_procs,	buffer);
	unpack_time(&credential_state_ptr->revoke_time,		buffer);
	unpack_time(&credential_state_ptr->expiration,		buffer);
	return SLURM_SUCCESS;

      unpack_error:
	return SLURM_ERROR;
}
