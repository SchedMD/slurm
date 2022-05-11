/*****************************************************************************\
 *  auth_munge.c - Slurm auth implementation via Chris Dunlap's Munge
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>
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

#include "config.h"

#include <inttypes.h>
#include <munge.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/uid.h"
#include "src/common/util-net.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#define RETRY_COUNT		20
#define RETRY_USEC		100000

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
const char plugin_name[] = "Munge authentication plugin";
const char plugin_type[] = "auth/munge";
const uint32_t plugin_id = AUTH_PLUGIN_MUNGE;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const bool hash_enable = true;

static int bad_cred_test = -1;

/*
 * The Munge implementation of the slurm AUTH credential
 */
#define MUNGE_MAGIC 0xfeed
typedef struct {
	int index; /* MUST ALWAYS BE FIRST. DO NOT PACK. */
	int magic;         /* magical munge validity magic                   */
	char   *m_str;     /* munged string                                  */
	struct in_addr addr; /* IP addr where cred was encoded               */
	bool    verified;  /* true if this cred has been verified            */
	uid_t   uid;       /* UID. valid only if verified == true            */
	gid_t   gid;       /* GID. valid only if verified == true            */
	void *data;        /* payload data */
	int dlen;          /* payload data length */
} auth_credential_t;

extern auth_credential_t *auth_p_create(char *opts, uid_t r_uid, void *data,
					int dlen);
extern int auth_p_destroy(auth_credential_t *cred);

/* Static prototypes */

static int _decode_cred(auth_credential_t *c, char *socket, bool test);
static void _print_cred(munge_ctx_t ctx);

/*
 *  Munge plugin initialization
 */
int init(void)
{
	int rc = SLURM_SUCCESS;
	char *fail_test_env = getenv("SLURM_MUNGE_AUTH_FAIL_TEST");
	if (fail_test_env)
		bad_cred_test = atoi(fail_test_env);
	else
		bad_cred_test = 0;

	/*
	 * MUNGE has a compile-time option that permits root to decode any
	 * credential regardless of the MUNGE_OPT_UID_RESTRICTION setting.
	 * This must not be enabled. Protect against it by ensuring we cannot
	 * decode a credential restricted to a different uid.
	 */
	if (running_in_daemon()) {
		auth_credential_t *cred = NULL;
		char *socket = slurm_auth_opts_to_socket(slurm_conf.authinfo);
		uid_t uid = getuid() + 1;

		cred = auth_p_create(slurm_conf.authinfo, uid, NULL, 0);
		if (!_decode_cred(cred, socket, true)) {
			error("MUNGE allows root to decode any credential");
			rc = SLURM_ERROR;
		}
		xfree(socket);
		auth_p_destroy(cred);
	}
	debug("%s loaded", plugin_name);
	return rc;
}


/*
 * Allocate a credential.  This function should return NULL if it cannot
 * allocate a credential.  Whether the credential is populated with useful
 * data at this time is implementation-dependent.
 */
auth_credential_t *auth_p_create(char *opts, uid_t r_uid, void *data, int dlen)
{
	int rc, retry = RETRY_COUNT, auth_ttl;
	auth_credential_t *cred = NULL;
	munge_err_t err = EMUNGE_SUCCESS;
	munge_ctx_t ctx = munge_ctx_create();
	SigFunc *ohandler;
	char *socket;

	if (!ctx) {
		error("munge_ctx_create failure");
		return NULL;
	}

	if (opts) {
		socket = slurm_auth_opts_to_socket(opts);
		rc = munge_ctx_set(ctx, MUNGE_OPT_SOCKET, socket);
		xfree(socket);
		if (rc != EMUNGE_SUCCESS) {
			error("munge_ctx_set failure");
			munge_ctx_destroy(ctx);
			return NULL;
		}
	}

	rc = munge_ctx_set(ctx, MUNGE_OPT_UID_RESTRICTION, r_uid);
	if (rc != EMUNGE_SUCCESS) {
		error("munge_ctx_set failure");
		munge_ctx_destroy(ctx);
		return NULL;
	}

	auth_ttl = slurm_get_auth_ttl();
	if (auth_ttl)
		(void) munge_ctx_set(ctx, MUNGE_OPT_TTL, auth_ttl);

	cred = xmalloc(sizeof(*cred));
	cred->magic = MUNGE_MAGIC;
	cred->verified = false;
	cred->m_str    = NULL;
	cred->data = NULL;
	cred->dlen = 0;

	/*
	 *  Temporarily block SIGALARM to avoid misleading
	 *    "Munged communication error" from libmunge if we
	 *    happen to time out the connection in this secion of
	 *    code. FreeBSD needs this cast.
	 */
	ohandler = xsignal(SIGALRM, (SigFunc *)SIG_BLOCK);

again:
	err = munge_encode(&cred->m_str, ctx, data, dlen);
	if (err != EMUNGE_SUCCESS) {
		if ((err == EMUNGE_SOCKET) && retry--) {
			debug("Munge encode failed: %s (retrying ...)",
			      munge_ctx_strerror(ctx));
			usleep(RETRY_USEC);	/* Likely munged too busy */
			goto again;
		}
		if (err == EMUNGE_SOCKET)
			error("If munged is up, restart with --num-threads=10");
		error("Munge encode failed: %s", munge_ctx_strerror(ctx));
		xfree(cred);
		cred = NULL;
		slurm_seterrno(ESLURM_AUTH_CRED_INVALID);
	} else if ((bad_cred_test > 0) && cred->m_str) {
		/*
		 * Avoid changing the trailing ':' character, or any of the
		 * trailing base64 padding which could leave the base64 stream
		 * intact, and fail to cause the failure we desire.
		 */
		int i = ((int) time(NULL)) % (strlen(cred->m_str) - 4);
		cred->m_str[i]++;	/* random position in credential */
	}

	xsignal(SIGALRM, ohandler);

	munge_ctx_destroy(ctx);

	return cred;
}

/*
 * Free a credential that was allocated with auth_p_create().
 */
int auth_p_destroy(auth_credential_t *cred)
{
	if (!cred) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_ERROR;
	}

	xassert(cred->magic == MUNGE_MAGIC);

	/* Note: Munge cred string not encoded with xmalloc() */
	if (cred->m_str)
		free(cred->m_str);
	if (cred->data)
		free(cred->data);

	xfree(cred);
	return SLURM_SUCCESS;
}

/*
 * Verify a credential to approve or deny authentication.
 *
 * Return SLURM_SUCCESS if the credential is in order and valid.
 */
int auth_p_verify(auth_credential_t *c, char *opts)
{
	int rc;
	char *socket;

	if (!c) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_ERROR;
	}

	xassert(c->magic == MUNGE_MAGIC);

	if (c->verified)
		return SLURM_SUCCESS;

	socket = slurm_auth_opts_to_socket(opts);
	rc = _decode_cred(c, socket, false);
	xfree(socket);
	if (rc < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

/*
 * Obtain the Linux UID from the credential.
 * auth_p_verify() must be called first.
 */
uid_t auth_p_get_uid(auth_credential_t *cred)
{
	if (!cred || !cred->verified) {
		/*
		 * This xassert will trigger on a development build if
		 * the calling path did not verify the credential first.
		 */
		xassert(!cred);
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_AUTH_NOBODY;
	}

	xassert(cred->magic == MUNGE_MAGIC);

	return cred->uid;
}

/*
 * Obtain the Linux GID from the credential.
 * auth_p_verify() must be called first.
 */
gid_t auth_p_get_gid(auth_credential_t *cred)
{
	if (!cred || !cred->verified) {
		/*
		 * This xassert will trigger on a development build if
		 * the calling path did not verify the credential first.
		 */
		xassert(!cred);
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_AUTH_NOBODY;
	}

	xassert(cred->magic == MUNGE_MAGIC);

	return cred->gid;
}


/*
 * Obtain the Host addr from where the credential originated.
 * auth_p_verify() must be called first.
 */
char *auth_p_get_host(auth_credential_t *cred)
{
	slurm_addr_t addr;
	struct sockaddr_in *sin = (struct sockaddr_in *) &addr;
	char *hostname = NULL, *dot_ptr = NULL;

	if (!cred || !cred->verified) {
		/*
		 * This xassert will trigger on a development build if
		 * the calling path did not verify the credential first.
		 */
		xassert(!cred);
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return NULL;
	}

	xassert(cred->magic == MUNGE_MAGIC);

	/* FIXME: this will need updates when MUNGE supports IPv6 addresses. */
	addr.ss_family = AF_INET;
	sin->sin_addr.s_addr = cred->addr.s_addr;

	/*
	 * For IPv6-native systems, MUNGE always reports the host as 0.0.0.0
	 * which will never resolve successfully. So don't even bother trying.
	 */
	if (sin->sin_addr.s_addr != 0) {
		hostname = xgetnameinfo((struct sockaddr *) &addr,
					sizeof(addr));
		/*
		 * The NI_NOFQDN flag was used here previously, but did not work
		 * as desired if the primary domain did not match on both sides.
		 */
		if (hostname && (dot_ptr = strchr(hostname, '.')))
			dot_ptr[0] = '\0';
	}

	if (!hostname) {
		/* at this point, the name lookup failed */
		hostname = xmalloc(INET_ADDRSTRLEN);
		slurm_get_ip_str(&addr, hostname, INET_ADDRSTRLEN);
		if (!(slurm_conf.conf_flags & CTL_CONF_IPV6_ENABLED))
			error("%s: Lookup failed for %s", __func__, hostname);
	}

	return hostname;
}

/*
 * auth_p_verify() must be called first.
 */
extern int auth_p_get_data(auth_credential_t *cred, char **data, uint32_t *len)
{
	if (!cred || !cred->verified) {
		/*
		 * This xassert will trigger on a development build if
		 * the calling path did not verify the credential first.
		 */
		xassert(!cred);
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_ERROR;
	}

	xassert(cred->magic == MUNGE_MAGIC);

	if (cred->data && cred->dlen) {
		*data = xmalloc(cred->dlen);
		memcpy(*data, cred->data, cred->dlen);
		*len = cred->dlen;
	} else {
		*data = NULL;
		*len = 0;
	}
	return SLURM_SUCCESS;
}

/*
 * Marshall a credential for transmission over the network, according to
 * Slurm's marshalling protocol.
 */
int auth_p_pack(auth_credential_t *cred, buf_t *buf, uint16_t protocol_version)
{
	if (!cred || !buf) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return SLURM_ERROR;
	}

	xassert(cred->magic == MUNGE_MAGIC);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(cred->m_str, buf);
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
auth_credential_t *auth_p_unpack(buf_t *buf, uint16_t protocol_version)
{
	auth_credential_t *cred = NULL;
	uint32_t size;

	if (!buf) {
		slurm_seterrno(ESLURM_AUTH_BADARG);
		return NULL;
	}

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/* Allocate and initialize credential. */
		cred = xmalloc(sizeof(*cred));
		cred->magic = MUNGE_MAGIC;
		cred->verified = false;
		cred->m_str = NULL;

		safe_unpackstr_malloc(&cred->m_str, &size, buf);
	} else {
		error("%s: unknown protocol version %u",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return cred;

unpack_error:
	slurm_seterrno(ESLURM_AUTH_UNPACK);
	auth_p_destroy(cred);
	return NULL;
}

/*
 * Decode the munge encoded credential `m_str' placing results, if validated,
 * into slurm credential `c'
 */
static int _decode_cred(auth_credential_t *c, char *socket, bool test)
{
	int retry = RETRY_COUNT;
	munge_err_t err;
	munge_ctx_t ctx;

	if (c == NULL)
		return SLURM_ERROR;

	xassert(c->magic == MUNGE_MAGIC);

	if (c->verified)
		return SLURM_SUCCESS;

	if ((ctx = munge_ctx_create()) == NULL) {
		error("munge_ctx_create failure");
		return SLURM_ERROR;
	}
	if (socket &&
	    (munge_ctx_set(ctx, MUNGE_OPT_SOCKET, socket) != EMUNGE_SUCCESS)) {
		error("munge_ctx_set failure");
		munge_ctx_destroy(ctx);
		return SLURM_ERROR;
	}

again:
	err = munge_decode(c->m_str, ctx, &c->data, &c->dlen, &c->uid, &c->gid);
	if (err != EMUNGE_SUCCESS) {
		if (test)
			goto done;
		if ((err == EMUNGE_SOCKET) && retry--) {
			debug("Munge decode failed: %s (retrying ...)",
			      munge_ctx_strerror(ctx));
			usleep(RETRY_USEC);	/* Likely munged too busy */
			goto again;
		}
		if (err == EMUNGE_SOCKET)
			error("If munged is up, restart with --num-threads=10");
#ifdef MULTIPLE_SLURMD
		/*
		 * In multiple slurmd mode this will happen all the time since
		 * we are authenticating with the same munged.
		 */
		if (err == EMUNGE_CRED_REPLAYED) {
			debug2("We had a replayed cred, but this is expected in multiple slurmd mode.");
			err = 0;
		} else {
#endif
			/*
			 *  Print any valid credential data
			 */
			error("Munge decode failed: %s",
			      munge_ctx_strerror(ctx));
			_print_cred(ctx);
			if (err == EMUNGE_CRED_REWOUND)
				error("Check for out of sync clocks");
			slurm_seterrno(ESLURM_AUTH_CRED_INVALID);
			goto done;
#ifdef MULTIPLE_SLURMD
		}
#endif
	}

	/*
	 * Store the addr so we can use it to verify where we came from later if
	 * needed.
	 */
	if (munge_ctx_get(ctx, MUNGE_OPT_ADDR4, &c->addr) != EMUNGE_SUCCESS)
		error("auth_munge: Unable to retrieve addr: %s",
		      munge_ctx_strerror(ctx));

	c->verified = true;

done:
	munge_ctx_destroy(ctx);
	return err ? SLURM_ERROR : SLURM_SUCCESS;
}

/*
 *  Print credential information.
 */
static void _print_cred(munge_ctx_t ctx)
{
	int e;
	char buf[256];
	time_t encoded, decoded;

	e = munge_ctx_get(ctx, MUNGE_OPT_ENCODE_TIME, &encoded);
	if (e != EMUNGE_SUCCESS)
		debug("%s: Unable to retrieve encode time: %s",
		      plugin_type, munge_ctx_strerror(ctx));
	else
		info("ENCODED: %s", slurm_ctime2_r(&encoded, buf));

	e = munge_ctx_get(ctx, MUNGE_OPT_DECODE_TIME, &decoded);
	if (e != EMUNGE_SUCCESS)
		debug("%s: Unable to retrieve decode time: %s",
		      plugin_type, munge_ctx_strerror(ctx));
	else
		info("DECODED: %s", slurm_ctime2_r(&decoded, buf));
}


/*
 * auth/munge does not support user aliasing. Only permit this call from the
 * same user (which means no internal state changes are necessary.
 */
int auth_p_thread_config(const char *token, const char *username)
{
	int rc = ESLURM_AUTH_CRED_INVALID;
	char *user;

	/* auth/munge does not accept user provided auth token */
	if (token || !username) {
		error("Rejecting thread config token for user %s", username);
		return rc;
	}

	user = uid_to_string_or_null(getuid());

	if (!xstrcmp(username, user)) {
		debug("applying thread config for user %s", username);
		rc = SLURM_SUCCESS;
	} else {
		error("rejecting thread config for user %s while running as %s",
		      username, user);
	}

	xfree(user);

	return rc;
}

void auth_p_thread_clear(void)
{
	/* no op */
}

char *auth_p_token_generate(const char *username, int lifespan)
{
	return NULL;
}
