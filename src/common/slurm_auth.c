/*****************************************************************************\
 *  slurm_auth.c - implementation-independent authentication API definitions
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

#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

static bool init_run = false;

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
	int		(*destroy)	(void *cred);
	int		(*verify)	(void *cred, char *auth_info);
	uid_t		(*get_uid)	(void *cred);
	gid_t		(*get_gid)	(void *cred);
	char *		(*get_host)	(void *cred);
	int		(*get_data)	(void *cred, char **data,
					 uint32_t *len);
	int		(*pack)		(void *cred, buf_t *buf,
					 uint16_t protocol_version);
	void *		(*unpack)	(buf_t *buf, uint16_t protocol_version);
	int		(*thread_config) (const char *token, const char *username);
	void		(*thread_clear) (void);
	char *		(*token_generate) (const char *username, int lifespan);
} slurm_auth_ops_t;
/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_auth_ops_t.
 */
static const char *syms[] = {
	"plugin_id",
	"plugin_type",
	"hash_enable",
	"auth_p_create",
	"auth_p_destroy",
	"auth_p_verify",
	"auth_p_get_uid",
	"auth_p_get_gid",
	"auth_p_get_host",
	"auth_p_get_data",
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
};

/*
 * A global authentication context.  "Global" in the sense that there's
 * only one, with static bindings.  We don't export it.
 */
static slurm_auth_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static int g_context_num = -1;
static pthread_mutex_t context_lock = PTHREAD_MUTEX_INITIALIZER;

extern bool slurm_get_plugin_hash_enable(int index)
{
	if (slurm_auth_init(NULL) < 0)
		return true;

	return *(ops[index].hash_enable);

}

extern int slurm_auth_init(char *auth_type)
{
	int retval = SLURM_SUCCESS;
	char *auth_alt_types = NULL, *list = NULL;
	char *type, *last = NULL;
	char *plugin_type = "auth";
	static bool daemon_run = false, daemon_set = false;

	if (init_run && (g_context_num > 0))
		return retval;

	slurm_mutex_lock(&context_lock);

	if (g_context_num > 0)
		goto done;

	if (getenv("SLURM_JWT")) {
		xfree(slurm_conf.authtype);
		slurm_conf.authtype = xstrdup("auth/jwt");
	} else if (auth_type) {
		xfree(slurm_conf.authtype);
		slurm_conf.authtype = xstrdup(auth_type);
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
		xrecalloc(ops, g_context_num + 1, sizeof(slurm_auth_ops_t));
		xrecalloc(g_context, g_context_num + 1,
			  sizeof(plugin_context_t));

		g_context[g_context_num] = plugin_context_create(
			plugin_type, type, (void **)&ops[g_context_num],
			syms, sizeof(syms));

		if (!g_context[g_context_num]) {
			error("cannot create %s context for %s", plugin_type, type);
			retval = SLURM_ERROR;
			goto done;
		}
		g_context_num++;

		if (auth_alt_types) {
			type = strtok_r(list, ",", &last);
			list = NULL; /* for next iteration */
		} else {
			type = NULL;
		}
	}
	init_run = true;

done:
	xfree(auth_alt_types);
	slurm_mutex_unlock(&context_lock);
	return retval;
}

/* Release all global memory associated with the plugin */
extern int slurm_auth_fini(void)
{
	int i, rc = SLURM_SUCCESS, rc2;

	slurm_mutex_lock(&context_lock);
	if (!g_context)
		goto done;

	init_run = false;

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
	slurm_mutex_unlock(&context_lock);
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
int slurm_auth_index(void *cred)
{
	cred_wrapper_t *wrapper = (cred_wrapper_t *) cred;

	if (wrapper)
		return wrapper->index;

	return 0;
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

	if (slurm_auth_init(NULL) < 0)
		return NULL;

	cred = (*(ops[index].create))(auth_info, r_uid, data, dlen);
	if (cred)
		cred->index = index;
	return cred;
}

int auth_g_destroy(void *cred)
{
	cred_wrapper_t *wrap = (cred_wrapper_t *) cred;

	if (!wrap || slurm_auth_init(NULL) < 0)
		return SLURM_ERROR;

	return (*(ops[wrap->index].destroy))(cred);
}

int auth_g_verify(void *cred, char *auth_info)
{
	cred_wrapper_t *wrap = (cred_wrapper_t *) cred;

	if (!wrap || slurm_auth_init(NULL) < 0)
		return SLURM_ERROR;

	return (*(ops[wrap->index].verify))(cred, auth_info);
}

uid_t auth_g_get_uid(void *cred)
{
	cred_wrapper_t *wrap = (cred_wrapper_t *) cred;

	if (!wrap || slurm_auth_init(NULL) < 0)
		return SLURM_AUTH_NOBODY;

	return (*(ops[wrap->index].get_uid))(cred);
}

gid_t auth_g_get_gid(void *cred)
{
	cred_wrapper_t *wrap = (cred_wrapper_t *) cred;

	if (!wrap || slurm_auth_init(NULL) < 0)
		return SLURM_AUTH_NOBODY;

	return (*(ops[wrap->index].get_gid))(cred);
}

char *auth_g_get_host(void *cred)
{
	cred_wrapper_t *wrap = (cred_wrapper_t *) cred;

	if (!wrap || slurm_auth_init(NULL) < 0)
		return NULL;

	return (*(ops[wrap->index].get_host))(cred);
}

int auth_g_get_data(void *cred, char **data, uint32_t *len)
{
	cred_wrapper_t *wrap = (cred_wrapper_t *) cred;

	if (!wrap || slurm_auth_init(NULL) < 0)
		return SLURM_ERROR;

	return (*(ops[wrap->index].get_data))(cred, data, len);
}

int auth_g_pack(void *cred, buf_t *buf, uint16_t protocol_version)
{
	cred_wrapper_t *wrap = (cred_wrapper_t *) cred;

	if (!wrap || slurm_auth_init(NULL) < 0)
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

void *auth_g_unpack(buf_t *buf, uint16_t protocol_version)
{
	uint32_t plugin_id = 0;
	cred_wrapper_t *cred;

	if (!buf || slurm_auth_init(NULL) < 0)
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
		error("%s: remote plugin_id %u not found",
		      __func__, plugin_id);
		return NULL;
	}  else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		return NULL;
	}

unpack_error:
	return NULL;
}

int auth_g_thread_config(const char *token, const char *username)
{
	if (slurm_auth_init(NULL) < 0)
		return SLURM_ERROR;

	return (*(ops[0].thread_config))(token, username);
}

void auth_g_thread_clear(void)
{
	if (slurm_auth_init(NULL) < 0)
		return;

	(*(ops[0].thread_clear))();
}

char *auth_g_token_generate(int plugin_id, const char *username,
			    int lifespan)
{
	if (slurm_auth_init(NULL) < 0)
		return NULL;

	for (int i = 0; i < g_context_num; i++) {
		if (plugin_id == *(ops[i].plugin_id)) {
			return (*(ops[i].token_generate))(username, lifespan);
		}
	}

	return NULL;
}
