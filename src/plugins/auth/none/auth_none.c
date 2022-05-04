/*****************************************************************************\
 *  auth_none.c - NO-OP slurm authentication plugin, validates all users.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Null authentication plugin";
const char plugin_type[] = "auth/none";
const uint32_t plugin_id = AUTH_PLUGIN_NONE;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
bool hash_enable = false;

/*
 * An opaque type representing authentication credentials.  This type can be
 * called whatever is meaningful and may contain any data required by the
 * plugin.  However, the plugin must be able to recover the Linux UID and
 * GID given only an object of this type.
 *
 * Since no verification of the credentials is performed in the "none"
 * authentication, this plugin simply uses the system-supplied UID and GID.
 * In a more robust authentication context, this might include tickets or
 * other signatures which the functions of this API can use to conduct
 * verification.
 *
 * The client code never sees the inside of this structure directly.
 * Objects of this type are passed in and out of the plugin via
 * anonymous pointers.  Because of this, robust plugins may wish to add
 * some form of runtime typing to ensure that the pointers they have
 * received are actually appropriate credentials and not pointers to
 * random memory.
 *
 * A word about thread safety.  The authentication plugin API specifies
 * that Slurm will exercise the plugin sanely.  That is, the authenticity
 * of a credential which has not been activated should not be tested.
 * However, the credential should be thread-safe.  This does not mean
 * necessarily that a plugin must recognize when an inconsistent sequence
 * of API calls is in progress, but if a plugin will crash or otherwise
 * misbehave if it is handed a credential in an inconsistent state (perhaps
 * it is in the process of being activated and a signature is incomplete)
 * then it is the plugin's responsibility to provide its own serialization\
 * to avoid that.
 *
 */
typedef struct _slurm_auth_credential {
	int index; /* MUST ALWAYS BE FIRST. DO NOT PACK. */
	char *hostname;
	uid_t uid;
	gid_t gid;
} slurm_auth_credential_t;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard Slurm authentication
 * API.
 */

/*
 * Allocate and initializes a credential.  This function should return
 * NULL if it cannot allocate a credential.
 */
slurm_auth_credential_t *slurm_auth_create(char *auth_info, uid_t r_uid,
					   void *data, int dlen)
{
	slurm_auth_credential_t *cred = xmalloc(sizeof(*cred));

	cred->uid = geteuid();
	cred->gid = getegid();

	cred->hostname = xshort_hostname();

	return cred;
}

/*
 * Free a credential that was allocated with slurm_auth_create() or
 * slurm_auth_unpack().
 */
int slurm_auth_destroy(slurm_auth_credential_t *cred)
{
	if (!cred) {
		slurm_seterrno(ESLURM_AUTH_MEMORY);
		return SLURM_ERROR;
	}
	xfree(cred->hostname);
	xfree(cred);
	return SLURM_SUCCESS;
}

/*
 * Verify a credential to approve or deny authentication.
 *
 * Return SLURM_SUCCESS if the credential is in order and valid.
 */
int slurm_auth_verify(slurm_auth_credential_t *cred, char *auth_info)
{
	return SLURM_SUCCESS;
}

/*
 * Obtain the Linux UID from the credential.  The accuracy of this data
 * is not assured until slurm_auth_verify() has been called for it.
 */
uid_t slurm_auth_get_uid(slurm_auth_credential_t *cred)
{
	if (!cred) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_AUTH_NOBODY;
	}

	return cred->uid;
}

/*
 * Obtain the Linux GID from the credential.
 * See slurm_auth_get_uid() above for details on correct behavior.
 */
gid_t slurm_auth_get_gid(slurm_auth_credential_t *cred)
{
	if (!cred) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_AUTH_NOBODY;
	}

	return cred->gid;
}

/*
 * Obtain the originating hostname from the credential.
 * See slurm_auth_get_uid() above for details on correct behavior.
 */
char *slurm_auth_get_host(slurm_auth_credential_t *cred)
{
	if (!cred) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return NULL;
	}

	return xstrdup(cred->hostname);
}

int auth_p_get_data(slurm_auth_credential_t *cred, char **data, uint32_t *len)
{
	if (!cred) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_ERROR;
	}

	*data = NULL;
	*len = 0;

	return SLURM_SUCCESS;
}

/*
 * Marshall a credential for transmission over the network, according to
 * Slurm's marshalling protocol.
 */
int slurm_auth_pack(slurm_auth_credential_t *cred, Buf buf,
		    uint16_t protocol_version)
{
	if (!cred || !buf) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_ERROR;
	}

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32((uint32_t) cred->uid, buf);
		pack32((uint32_t) cred->gid, buf);
		packstr(cred->hostname, buf);
	} else {
		error("%s: Unknown protocol version %d",
		      __func__, protocol_version);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * Unmarshall a credential after transmission over the network according
 * to Slurm's marshalling protocol.
 */
slurm_auth_credential_t *slurm_auth_unpack(Buf buf, uint16_t protocol_version)
{
	slurm_auth_credential_t *cred = NULL;
	uint32_t tmpint;
	uint32_t uint32_tmp = 0;

	if (!buf) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return NULL;
	}

	/* Allocate a new credential. */
	cred = xmalloc(sizeof(*cred));

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/*
		 * We do it the hard way because we don't know anything about
		 * the size of uid_t or gid_t, only that they are integer
		 * values.  We pack them as 32-bit integers, but we can't pass
		 * addresses to them directly to unpack as 32-bit integers
		 * because there will be bad clobbering if they really aren't.
		 * This technique ensures a warning at compile time if the sizes
		 * are incompatible.
		 */
		safe_unpack32(&tmpint, buf);
		cred->uid = tmpint;
		safe_unpack32(&tmpint, buf);
		cred->gid = tmpint;
		safe_unpackstr_xmalloc(&cred->hostname, &uint32_tmp, buf);
	} else {
		error("%s: unknown protocol version %u",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return cred;

unpack_error:
	slurm_auth_destroy(cred);
	slurm_seterrno(ESLURM_AUTH_UNPACK);
	return NULL;
}

int slurm_auth_thread_config(const char *token, const char *username)
{
	/* No auth -> everything works */
	return SLURM_SUCCESS;
}

void slurm_auth_thread_clear(void)
{
	/* no op */
}

char *slurm_auth_token_generate(const char *username, int lifespan)
{
	return NULL;
}
