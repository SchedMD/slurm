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
	void *		(*create)	(char *auth_info);
	int		(*destroy)	(void *cred);
	int		(*verify)	(void *cred, char *auth_info);
	uid_t		(*get_uid)	(void *cred);
	gid_t		(*get_gid)	(void *cred);
	char *		(*get_host)	(void *cred);
	int		(*pack)		(void *cred, Buf buf,
					 uint16_t protocol_version);
	void *		(*unpack)	(Buf buf, uint16_t protocol_version);
} slurm_auth_ops_t;
/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_auth_ops_t.
 */
static const char *syms[] = {
	"plugin_id",
	"plugin_type",
	"slurm_auth_create",
	"slurm_auth_destroy",
	"slurm_auth_verify",
	"slurm_auth_get_uid",
	"slurm_auth_get_gid",
	"slurm_auth_get_host",
	"slurm_auth_pack",
	"slurm_auth_unpack",
};

/*
 * A global authentication context.  "Global" in the sense that there's
 * only one, with static bindings.  We don't export it.
 */
static slurm_auth_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static int g_context_num = -1;
static pthread_mutex_t context_lock = PTHREAD_MUTEX_INITIALIZER;

extern int slurm_auth_init(char *auth_type)
{
	int retval = SLURM_SUCCESS;
	char *auth_alt_types = NULL, *list = NULL;
	char *auth_plugin_type = NULL, *type, *last = NULL;
	char *plugin_type = "auth";

	if (init_run && (g_context_num > 0))
		return retval;

	slurm_mutex_lock(&context_lock);

	if (g_context_num > 0)
		goto done;

	if (auth_type)
		slurm_set_auth_type(auth_type);

	type = auth_plugin_type = slurm_get_auth_type();
	if (run_in_daemon("slurmctld,slurmdbd"))
		list = auth_alt_types = slurm_get_auth_alt_types();
	g_context_num = 0;
	if (!auth_plugin_type || auth_plugin_type[0] == '\0')
		goto done;

	/*
	 * This loop construct ensures that the AuthType is in position zero
	 * of the ops and g_context arrays, followed by any AuthAltTypes that
	 * have been defined. This ensures that the most common type is found
	 * first in g_slurm_auth_unpack(), and that we can default to
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
	xfree(auth_plugin_type);
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

void *g_slurm_auth_create(int index, char *auth_info)
{
	cred_wrapper_t *cred;

	if (slurm_auth_init(NULL) < 0)
		return NULL;

	cred = (*(ops[index].create))(auth_info);
	if (cred)
		cred->index = index;
	return cred;
}

int g_slurm_auth_destroy(void *cred)
{
	cred_wrapper_t *wrap = (cred_wrapper_t *) cred;

	if (!wrap || slurm_auth_init(NULL) < 0)
		return SLURM_ERROR;

	return (*(ops[wrap->index].destroy))(cred);
}

int g_slurm_auth_verify(void *cred, char *auth_info)
{
	cred_wrapper_t *wrap = (cred_wrapper_t *) cred;

	if (!wrap || slurm_auth_init(NULL) < 0)
		return SLURM_ERROR;

	return (*(ops[wrap->index].verify))(cred, auth_info);
}

uid_t g_slurm_auth_get_uid(void *cred)
{
	cred_wrapper_t *wrap = (cred_wrapper_t *) cred;

	if (!wrap || slurm_auth_init(NULL) < 0)
		return SLURM_AUTH_NOBODY;

	return (*(ops[wrap->index].get_uid))(cred);
}

gid_t g_slurm_auth_get_gid(void *cred)
{
	cred_wrapper_t *wrap = (cred_wrapper_t *) cred;

	if (!wrap || slurm_auth_init(NULL) < 0)
		return SLURM_AUTH_NOBODY;

	return (*(ops[wrap->index].get_gid))(cred);
}

char *g_slurm_auth_get_host(void *cred)
{
	cred_wrapper_t *wrap = (cred_wrapper_t *) cred;

	if (!wrap || slurm_auth_init(NULL) < 0)
		return NULL;

	return (*(ops[wrap->index].get_host))(cred);
}

int g_slurm_auth_pack(void *cred, Buf buf, uint16_t protocol_version)
{
	cred_wrapper_t *wrap = (cred_wrapper_t *) cred;

	if (!wrap || slurm_auth_init(NULL) < 0)
		return SLURM_ERROR;

	if (protocol_version >= SLURM_19_05_PROTOCOL_VERSION) {
		pack32(*ops[wrap->index].plugin_id, buf);
		return (*(ops[wrap->index].pack))(cred, buf, protocol_version);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(ops[wrap->index].plugin_type, buf);
		/*
		 * This next field was packed with plugin_version within each
		 * individual auth plugin, but upon unpack was never checked
		 * against anything. Rather than expose the protocol_version
		 * symbol, just pack a zero here instead.
		 */
		pack32(0, buf);
		return (*(ops[wrap->index].pack))(cred, buf, protocol_version);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		return SLURM_ERROR;
	}
}

void *g_slurm_auth_unpack(Buf buf, uint16_t protocol_version)
{
	uint32_t plugin_id = 0;
	cred_wrapper_t *cred;

	if (!buf || slurm_auth_init(NULL) < 0)
		return NULL;

	if (protocol_version >= SLURM_19_05_PROTOCOL_VERSION) {
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
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		char *plugin_type;
		uint32_t uint32_tmp, version;
		safe_unpackmem_ptr(&plugin_type, &uint32_tmp, buf);
		safe_unpack32(&version, buf);
		for (int i = 0; i < g_context_num; i++) {
			if (!xstrcmp(plugin_type, ops[i].plugin_type)) {
				cred = (*(ops[i].unpack))(buf,
							  protocol_version);
				if (cred)
					cred->index = i;
				return cred;
			}
		}
		error("%s: remote plugin_type %s not found",
		      __func__, plugin_type);
		return NULL;
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		return NULL;
	}

unpack_error:
	return NULL;
}
