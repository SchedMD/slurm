/*****************************************************************************\
 *  credential_utils.c - slurm authentication credential management functions
 *  $Id$
 *****************************************************************************
 *  Written by Kevin Tew <tewk@llnl.gov>
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

#include <slurm/slurm_errno.h>

#include "src/common/credential_utils.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/signature_utils.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"

#define MAX_NAME_LEN 1024

/* global variables */

/* prototypes */

static int  _clear_expired_revoked_credentials(List list);
static int  _is_credential_still_valid(slurm_job_credential_t *, List);
static void _free_credential_state(void *credential_state);
static int  _insert_credential_state(slurm_job_credential_t *l, List);
static int  _insert_revoked_credential_state(revoke_credential_msg_t *, List);
static void _pack_one_cred(credential_state_t *, Buf);
static int  _unpack_one_cred(credential_state_t *, Buf);

static int  _init_credential_state( credential_state_t *, 
		                    slurm_job_credential_t *);

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

void 
print_credential(slurm_job_credential_t * cred)
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

int 
revoke_credential(revoke_credential_msg_t * msg, List list)
{
	time_t              now   = time(NULL);
	uint32_t            jobid = msg->job_id;
	ListIterator        i     = NULL;
	credential_state_t *state = NULL;

	i = list_iterator_create(list);

	while ( (state = list_next(i)) && (state->job_id != jobid) ) {;}

	list_iterator_destroy(i);

	if (state) {
		state->revoked     = true;
		state->revoke_time = now;
	} else
		_insert_revoked_credential_state(msg, list);

	return SLURM_SUCCESS;
}



static int
_is_credential_still_valid(slurm_job_credential_t * credential, List list)
{
	uint32_t            jobid = credential->job_id;
	ListIterator        i     = NULL;
	credential_state_t *state = NULL;

	_clear_expired_revoked_credentials(list);

	i = list_iterator_create(list);

	while ( (state = list_next(i)) && (state->job_id != jobid)) {;}

	list_iterator_destroy(i);

	if (!state)
		_insert_credential_state(credential, list);
	else if (state->revoked)
		return ESLURMD_CREDENTIAL_REVOKED;

	return SLURM_SUCCESS;
}

void
clear_expired_credentials(List l)
{
	_clear_expired_revoked_credentials(l);
}

/*
 * This function is not thread-safe. However, it should only
 * be used from _clear_expired_revoked_credentials(), below,
 * which is only called from a single thread.
 */
static char *
_cred_string(uint32_t jobid)
{
	static char buf[256];
	snprintf(buf, sizeof(buf), "job%d", jobid);
	return buf;
}

static void
_print_expired_list(hostlist_t hl)
{
	char buf[1024];

	xassert(hl != NULL);

	if (!hostlist_count(hl))
		return;

	hostlist_ranged_string(hl, sizeof(buf), buf);
	debug2("expired credentials for: %s", buf);
}

static int 
_clear_expired_revoked_credentials(List list)
{
	time_t now = time(NULL);
	ListIterator iterator;
	credential_state_t *s;
	hostlist_t hl = hostlist_create(NULL);

	debug2("clearing expired credentials");

	iterator = list_iterator_create(list);
	while ((s = list_next(iterator))) {
		if (now > (s->expiration + EXPIRATION_WINDOW) ) {
			hostlist_push(hl, _cred_string(s->job_id));
			list_delete(iterator);
		}
	}
	list_iterator_destroy(iterator);

	_print_expired_list(hl);
	hostlist_destroy(hl);

	return SLURM_SUCCESS;
}

bool 
credential_is_cached(List list, uint32_t jobid)
{
	ListIterator i;
	credential_state_t *state;

	debug2("checking for cached credential for job %u", jobid);

	i = list_iterator_create(list);
	while ( (state = list_next(i)) && (state->job_id != jobid) ) {;}
	list_iterator_destroy(i);

	return (state != NULL);
}

int 
initialize_credential_state_list(List * list)
{
	*list = list_create((ListDelF) _free_credential_state);
	return SLURM_SUCCESS;
}

int 
destroy_credential_state_list(List list)
{
	list_destroy(list);
	return SLURM_SUCCESS;
}

static int
_init_credential_state(credential_state_t * credential_state,
		      slurm_job_credential_t * credential)
{
	credential_state->job_id	= credential->job_id;
	credential_state->expiration	= credential->expiration_time;
	credential_state->revoked	= false;
	return SLURM_SUCCESS;
}

static void 
_free_credential_state(void *state)
{
	if (state) {
		xfree(state);
	}
}

static int 
_insert_credential_state(slurm_job_credential_t * credential, List list)
{
	credential_state_t *s = xmalloc(sizeof(*s));

	_init_credential_state(s, credential);
	list_append(list, s);

	return SLURM_SUCCESS;
}

int
_insert_revoked_credential_state(revoke_credential_msg_t *msg, List list)
{
	time_t now = time(NULL);
	credential_state_t *s = xmalloc(sizeof(*s));

	s->job_id      = msg->job_id;
	s->expiration  = msg->expiration_time;
	s->revoked     = true;
	s->revoke_time = now;

	list_append(list, s);
	return SLURM_SUCCESS;
}

/* pack_credential_list
 * pack a list of credentials into a machine independent format buffer
 * IN list		- list to credentials to pack
 * IN/OUT buffer	- existing buffer into which the credential
 *			  information should be stored
 */ 
void 
pack_credential_list(List list, Buf buffer)
{
	ListIterator        i = NULL;
	credential_state_t *s = NULL;

	i = list_iterator_create(list);
	while ((s = list_next(i)))
		_pack_one_cred(s, buffer);
	list_iterator_destroy(i);
}

/* unpack_credential_list
 * unpack a list of credentials from a machine independent format buffer
 * IN/OUT list		- existing list onto which the records in 
 *			  the buffer are added
 * IN buffer		- existing buffer from which the credential
 *			  information should be read
 * RET int		- zero or error code
 */ 
int 
unpack_credential_list(List list, Buf buffer)
{
	credential_state_t *s = NULL;

	do {
		s = xmalloc(sizeof(slurm_job_credential_t));
		if (_unpack_one_cred(s, buffer)) {
			xfree(s);
			return SLURM_ERROR;
		} else	
			list_append(list, s);
	} while (remaining_buf(buffer));

	return SLURM_SUCCESS;
}

static void 
_pack_one_cred(credential_state_t *state, Buf buffer)
{
	pack32(state->job_id,		buffer);
	pack16(state->revoked,		buffer);
	pack16(state->procs_allocated,	buffer);
	pack16(state->total_procs,	buffer);
	pack_time(state->revoke_time,	buffer);
	pack_time(state->expiration,	buffer);
}

static int 
_unpack_one_cred(credential_state_t *state, Buf buffer)
{
	safe_unpack32(&state->job_id,		buffer);
	safe_unpack16(&state->revoked,		buffer);
	safe_unpack16(&state->procs_allocated,	buffer);
	safe_unpack16(&state->total_procs,	buffer);
	unpack_time(&state->revoke_time,	buffer);
	unpack_time(&state->expiration,		buffer);
	return SLURM_SUCCESS;

      unpack_error:
	return SLURM_ERROR;
}
