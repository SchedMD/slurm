/*****************************************************************************\
 *  auth.c - implementation-independent authentication API definitions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>
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

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/util-net.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/auth.h"

typedef struct {
	int index;
	char data[];
} cred_wrapper_t;

typedef struct {
	uint32_t	(*plugin_id);
	char		(*plugin_type);
	bool		(*hash_enable);
	void *		(*create)	(char *auth_info, uid_t r_uid,
					 void *data, int dlen);
	void		(*destroy)	(void *cred);
	int		(*verify)	(void *cred, char *auth_info);
	void		(*get_ids)	(void *cred, uid_t *uid, gid_t *gid);
	char *		(*get_host)	(void *cred);
	int		(*get_data)	(void *cred, char **data,
					 uint32_t *len);
	void *		(*get_identity)	(void *cred);
	int		(*pack)		(void *cred, buf_t *buf,
					 uint16_t protocol_version);
	void *		(*unpack)	(buf_t *buf, uint16_t protocol_version);
	int		(*thread_config) (const char *token, const char *username);
	void		(*thread_clear) (void);
	char *		(*token_generate) (const char *username, int lifespan);
} auth_ops_t;
/*
 * These strings must be kept in the same order as the fields
 * declared for auth_ops_t.
 */
static const char *syms[] = {
	"plugin_id",
	"plugin_type",
	"hash_enable",
	"auth_p_create",
	"auth_p_destroy",
	"auth_p_verify",
	"auth_p_get_ids",
	"auth_p_get_host",
	"auth_p_get_data",
	"auth_p_get_identity",
	"auth_p_pack",
	"auth_p_unpack",
	"auth_p_thread_config",
	"auth_p_thread_clear",
	"auth_p_token_generate",
};

typedef struct {
	int plugin_id;
	char *type;
} auth_plugin_types_t;

auth_plugin_types_t auth_plugin_types[] = {
	{ AUTH_PLUGIN_NONE, "auth/none" },
	{ AUTH_PLUGIN_MUNGE, "auth/munge" },
	{ AUTH_PLUGIN_JWT, "auth/jwt" },
	{ AUTH_PLUGIN_SLURM, "auth/slurm" },
};

/*
 * A global authentication context.  "Global" in the sense that there's
 * only one, with static bindings.  We don't export it.
 */
static auth_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static int g_context_num = -1;
static pthread_rwlock_t context_lock = PTHREAD_RWLOCK_INITIALIZER;

static bool at_forked = false;
static bool externally_locked = false;
static void _atfork_child()
{
	slurm_rwlock_init(&context_lock);

	/*
	 * If we're in _drop_privileges() when we fork we need to hold the lock
	 * in the child process to prevent any other auth plugin calls until
	 * _reclaim_privileges(). However, for rwlocks, you cannot simply call
	 * slurm_rwlock_unlock() in the pthread_atfork() child handler -
	 * testing demonstrated that this won't unlock and you'll be deadlocked
	 * on the next lock acquisition.
	 *
	 * (This does work for slurm_mutex_unlock() - as is used in other
	 * pthread_atfork() child handlers within Slurm. This appears to be
	 * something specific to pthread_rwlock_t's construction.)
	 *
	 * Instead, reacquire the lock immediately after the
	 * slurm_rwlock_init() call. Since we're in the child process here, the
	 * eventual _reclaim_privileges() call to auth_setuid_unlock() will
	 * behave as expected.
	 */
	if (externally_locked)
		slurm_rwlock_wrlock(&context_lock);
}

extern const char *auth_get_plugin_name(int plugin_id)
{
	for (int i = 0; i < ARRAY_SIZE(auth_plugin_types); i++)
		if (plugin_id == auth_plugin_types[i].plugin_id)
			return auth_plugin_types[i].type;

	return "unknown";
}

extern bool slurm_get_plugin_hash_enable(int index)
{
	xassert(g_context_num > 0);
	if (g_context_num <= 0)
		fatal("No hash plugins loaded. Was slurm_init() called before calling any Slurm API functions?");

	return *(ops[index].hash_enable);
}

extern bool auth_is_plugin_type_inited(int plugin_id)
{
	for (int i = 0; i < g_context_num; i++)
		if (plugin_id == *(ops[i].plugin_id))
			return true;
	return false;
}

extern int auth_g_init(void)
{
	int retval = SLURM_SUCCESS;
	char *auth_alt_types = NULL, *list = NULL;
	char *type, *last = NULL;
	char *plugin_type = "auth";
	static bool daemon_run = false, daemon_set = false;

	slurm_rwlock_wrlock(&context_lock);

	if (g_context_num > 0)
		goto done;

	if (getenv("SLURM_JWT")) {
		xfree(slurm_conf.authtype);
		slurm_conf.authtype = xstrdup(
			auth_get_plugin_name(AUTH_PLUGIN_JWT));
	}

	if (getenv("SLURM_SACK_KEY")) {
		xfree(slurm_conf.authtype);
		slurm_conf.authtype = xstrdup(
			auth_get_plugin_name(AUTH_PLUGIN_SLURM));
	}

	type = slurm_conf.authtype;
	if (!type || type[0] == '\0')
		goto done;

	if (run_in_daemon(&daemon_run, &daemon_set, "slurmctld,slurmdbd"))
		list = auth_alt_types = xstrdup(slurm_conf.authalttypes);
	g_context_num = 0;

	/*
	 * This loop construct ensures that the AuthType is in position zero
	 * of the ops and g_context arrays, followed by any AuthAltTypes that
	 * have been defined. This ensures that the most common type is found
	 * first in auth_g_unpack(), and that we can default to
	 * the zeroth element rather than tracking the primary plugin
	 * through some other index.
	 * One other side effect is that the AuthAltTypes are permitted to
	 * be comma separated, vs. AuthType which can have only one value.
	 */
	while (type) {
		xrecalloc(ops, g_context_num + 1, sizeof(auth_ops_t));
		xrecalloc(g_context, g_context_num + 1,
			  sizeof(plugin_context_t));

		if (!xstrncmp(type, "auth/", 5))
			type += 5;
		type = xstrdup_printf("auth/%s", type);

		g_context[g_context_num] = plugin_context_create(
			plugin_type, type, (void **)&ops[g_context_num],
			syms, sizeof(syms));

		if (!g_context[g_context_num]) {
			error("cannot create %s context for %s", plugin_type, type);
			retval = SLURM_ERROR;
			xfree(type);
			goto done;
		}
		g_context_num++;
		xfree(type);

		if (auth_alt_types) {
			type = strtok_r(list, ",", &last);
			list = NULL; /* for next iteration */
		}
	}
done:
	if (!at_forked) {
		pthread_atfork(NULL, NULL, _atfork_child);
		at_forked = true;
	}

	xfree(auth_alt_types);
	slurm_rwlock_unlock(&context_lock);
	return retval;
}

/* Release all global memory associated with the plugin */
extern int auth_g_fini(void)
{
	int i, rc = SLURM_SUCCESS, rc2;

	slurm_rwlock_wrlock(&context_lock);
	if (!g_context)
		goto done;

	for (i = 0; i < g_context_num; i++) {
		rc2 = plugin_context_destroy(g_context[i]);
		if (rc2) {
			debug("%s: %s: %s",
			      __func__, g_context[i]->type,
			      slurm_strerror(rc2));
			rc = SLURM_ERROR;
		}
	}

	xfree(ops);
	xfree(g_context);
	g_context_num = -1;

done:
	slurm_rwlock_unlock(&context_lock);
	return rc;
}

/*
 * Retrieve the auth_index corresponding to the authentication
 * plugin used to create a given credential.
 *
 * Note that this works because all plugin credential types
 * are required to store the auth_index as an int first in their
 * internal (opaque) structures.
 *
 * The cast through cred_wrapper_t then gives us convenient access
 * to that auth_index value.
 */
extern int auth_index(void *cred)
{
	cred_wrapper_t *wrapper = cred;

	if (wrapper)
		return wrapper->index;

	return 0;
}

extern void auth_setuid_lock(void)
{
	slurm_rwlock_wrlock(&context_lock);
	/*
	 * If running under _drop_privileges(), we want the locked state
	 * to persist after fork() as it is still not safe to use the
	 * rest of the auth API until after _reclaim_privileges().
	 */
	externally_locked = true;
}

extern void auth_setuid_unlock(void)
{
	externally_locked = false;
	slurm_rwlock_unlock(&context_lock);
}

/*
 * Static bindings for the global authentication context.  The test
 * of the function pointers is omitted here because the global
 * context initialization includes a test for the completeness of
 * the API function dispatcher.
 */

void *auth_g_create(int index, char *auth_info, uid_t r_uid,
		    void *data, int dlen)
{
	cred_wrapper_t *cred;

	xassert(g_context_num > 0);

	if (r_uid == SLURM_AUTH_NOBODY)
		return NULL;

	slurm_rwlock_rdlock(&context_lock);
	cred = (*(ops[index].create))(auth_info, r_uid, data, dlen);
	slurm_rwlock_unlock(&context_lock);

	if (cred)
		cred->index = index;
	return cred;
}

extern void auth_g_destroy(void *cred)
{
	cred_wrapper_t *wrap = cred;

	xassert(g_context_num > 0);

	if (!wrap)
		return;

	(*(ops[wrap->index].destroy))(cred);
}

extern int auth_g_verify(void *cred, char *auth_info)
{
	int rc = SLURM_ERROR;
	cred_wrapper_t *wrap = cred;

	xassert(g_context_num > 0);

	if (!wrap)
		return SLURM_ERROR;

	slurm_rwlock_rdlock(&context_lock);
	rc = (*(ops[wrap->index].verify))(cred, auth_info);
	slurm_rwlock_unlock(&context_lock);

	return rc;
}

extern void auth_g_get_ids(void *cred, uid_t *uid, gid_t *gid)
{
	cred_wrapper_t *wrap = cred;

	xassert(g_context_num > 0);

	*uid = SLURM_AUTH_NOBODY;
	*gid = SLURM_AUTH_NOBODY;

	if (!wrap)
		return;

	slurm_rwlock_rdlock(&context_lock);
	(*(ops[wrap->index].get_ids))(cred, uid, gid);
	slurm_rwlock_unlock(&context_lock);
}

extern uid_t auth_g_get_uid(void *cred)
{
	cred_wrapper_t *wrap = cred;
	uid_t uid = SLURM_AUTH_NOBODY;
	gid_t gid = SLURM_AUTH_NOBODY;

	xassert(g_context_num > 0);

	if (!wrap)
		return SLURM_AUTH_NOBODY;

	slurm_rwlock_rdlock(&context_lock);
	(*(ops[wrap->index].get_ids))(cred, &uid, &gid);
	slurm_rwlock_unlock(&context_lock);

	return uid;
}

extern char *auth_g_get_host(void *slurm_msg)
{
	slurm_addr_t addr;
	slurm_msg_t *msg = slurm_msg;
	cred_wrapper_t *wrap = NULL;
	char *host = NULL;

	xassert(g_context_num > 0);

	if (!msg || !(wrap = msg->auth_cred))
		return NULL;

	slurm_rwlock_rdlock(&context_lock);
	host = (*(ops[wrap->index].get_host))(wrap);
	slurm_rwlock_unlock(&context_lock);

	if (host) {
		debug3("%s: using auth token: %s", __func__, host);
		return host;
	}

	if (msg->conn && msg->conn->rem_host) {
		/* use remote host name if persistent connection */
		host = xstrdup(msg->conn->rem_host);
		debug3("%s: using remote hostname: %s", __func__, host);
		return host;
	}

	if (slurm_get_peer_addr(msg->conn_fd, &addr)) {
		error("%s: unable to determine host", __func__);
		return NULL;
	}

	/* use remote host IP, then look it up */
	if ((host = xgetnameinfo((struct sockaddr *) &addr, sizeof(addr)))) {
		debug3("%s: looked up from connection's IP address: %s",
		       __func__, host);
	} else {
		host = xmalloc(INET6_ADDRSTRLEN);
		slurm_get_ip_str(&addr, host, INET6_ADDRSTRLEN);
		debug3("%s: using connection's IP address: %s", __func__, host);
	}

	return host;
}

extern int auth_g_get_data(void *cred, char **data, uint32_t *len)
{
	cred_wrapper_t *wrap = cred;
	int rc = SLURM_ERROR;

	xassert(g_context_num > 0);

	if (!wrap)
		return SLURM_ERROR;

	slurm_rwlock_rdlock(&context_lock);
	rc = (*(ops[wrap->index].get_data))(cred, data, len);
	slurm_rwlock_unlock(&context_lock);

	return rc;
}

extern void *auth_g_get_identity(void *cred)
{
	cred_wrapper_t *wrap = cred;
	void *id = NULL;

	xassert(g_context_num > 0);

	if (!wrap)
		return NULL;

	slurm_rwlock_rdlock(&context_lock);
	id = (*(ops[wrap->index].get_identity))(cred);
	slurm_rwlock_unlock(&context_lock);

	return id;
}

extern int auth_g_pack(void *cred, buf_t *buf, uint16_t protocol_version)
{
	cred_wrapper_t *wrap = cred;

	xassert(g_context_num > 0);

	if (!wrap)
		return SLURM_ERROR;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(*ops[wrap->index].plugin_id, buf);
		return (*(ops[wrap->index].pack))(cred, buf, protocol_version);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		return SLURM_ERROR;
	}
}

extern void *auth_g_unpack(buf_t *buf, uint16_t protocol_version)
{
	uint32_t plugin_id = 0;
	cred_wrapper_t *cred;

	xassert(g_context_num > 0);

	if (!buf)
		return NULL;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&plugin_id, buf);
		for (int i = 0; i < g_context_num; i++) {
			if (plugin_id == *(ops[i].plugin_id)) {
				cred = (*(ops[i].unpack))(buf,
							  protocol_version);
				if (cred)
					cred->index = i;
				return cred;
			}
		}
		error("%s: authentication plugin %s(%u) not found",
		      __func__, auth_get_plugin_name(plugin_id), plugin_id);
		return NULL;
	}  else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		return NULL;
	}

unpack_error:
	return NULL;
}

extern int auth_g_thread_config(const char *token, const char *username)
{
	int rc = SLURM_SUCCESS;
	xassert(g_context_num > 0);

	slurm_rwlock_rdlock(&context_lock);
	rc = (*(ops[0].thread_config))(token, username);
	slurm_rwlock_unlock(&context_lock);

	return rc;
}

extern void auth_g_thread_clear(void)
{
	xassert(g_context_num > 0);

	slurm_rwlock_rdlock(&context_lock);
	(*(ops[0].thread_clear))();
	slurm_rwlock_unlock(&context_lock);
}

extern char *auth_g_token_generate(int plugin_id, const char *username,
				   int lifespan)
{
	char *token = NULL;
	xassert(g_context_num > 0);

	slurm_rwlock_rdlock(&context_lock);
	for (int i = 0; i < g_context_num; i++) {
		if (plugin_id == *(ops[i].plugin_id)) {
			token = (*(ops[i].token_generate))(username, lifespan);
			break;
		}
	}
	slurm_rwlock_unlock(&context_lock);

	return token;
}
