/*****************************************************************************\
 *  auth_slurm.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/identity.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/run_in_daemon.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/auth.h"
#include "src/interfaces/serializer.h"

#include "src/plugins/auth/slurm/auth_slurm.h"

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
const char plugin_name[] = "Slurm auth and cred plugin";
const char plugin_type[] = "auth/slurm";
const uint32_t plugin_id = AUTH_PLUGIN_SLURM;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const bool hash_enable = true;

bool internal = false;
bool use_client_ids = false;

static void _run_sack_maybe(void)
{
	bool run_sack = true;

	if (xstrstr(slurm_conf.authinfo, "disable_sack"))
		run_sack = false;

	if (running_in_sackd())
		run_sack = true;

	if (getenv("SLURM_CONFIG_FETCH"))
		run_sack = false;

	if (run_sack)
		init_sack_conmgr();
}

extern int init(void)
{
	static bool init_run = false;
	bool run = false, set = false;

	if (init_run)
		return SLURM_SUCCESS;
	init_run = true;

	if (serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL))
		fatal("%s: serializer_g_init() failed", __func__);

	internal = run_in_daemon(&run, &set, "sackd,slurmd,slurmctld,slurmdbd");

	if (internal) {
		debug("running as daemon");
		init_internal();
		_run_sack_maybe();
	} else {
		debug("running as client");
	}

	if (xstrstr(slurm_conf.authinfo, "use_client_ids"))
		use_client_ids = true;

	debug("loaded: internal=%s, use_client_ids=%s",
	      internal ? "true" : "false",
	      use_client_ids ? "true" : "false");

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	static bool fini_run = false;

	if (fini_run)
		return SLURM_SUCCESS;
	fini_run = true;

	if (internal) {
		fini_sack_conmgr();
		fini_internal();
	}

	return SLURM_SUCCESS;
}

extern auth_cred_t *auth_p_create(char *auth_info, uid_t r_uid, void *data,
				  int dlen)
{
	if (internal) {
		auth_cred_t *cred = new_cred();
		cred->token = create_internal("auth", getuid(), getgid(), r_uid,
					      data, dlen, NULL);
		return cred;
	}

	return create_external(r_uid, data, dlen);
}

extern void auth_p_destroy(auth_cred_t *cred)
{
	destroy_cred(cred);
}

extern int auth_p_verify(auth_cred_t *cred, char *auth_info)
{
	if (!cred) {
		errno = ESLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}

	if (internal)
		return verify_internal(cred, getuid());

	return verify_external(cred);
}

extern void auth_p_get_ids(auth_cred_t *cred, uid_t *uid, gid_t *gid)
{
	if (!cred || !cred->verified) {
		/*
		 * This xassert will trigger on a development build if
		 * the calling path did not verify the credential first.
		 */
		xassert(!cred);
		*uid = SLURM_AUTH_NOBODY;
		*gid = SLURM_AUTH_NOBODY;
		return;
	}

	*uid = cred->uid;
	*gid = cred->gid;
}

extern char *auth_p_get_host(auth_cred_t *cred)
{
	if (!cred) {
		errno = ESLURM_AUTH_BADARG;
		return NULL;
	}

	return xstrdup(cred->hostname);
}

extern int auth_p_get_data(auth_cred_t *cred, char **data, uint32_t *len)
{
	if (!cred) {
		errno = ESLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}

	*data = cred->data;
	*len = cred->dlen;
	cred->data = NULL;
	cred->dlen = 0;
	return SLURM_SUCCESS;
}

extern void *auth_p_get_identity(auth_cred_t *cred)
{
	if (!cred)
		return NULL;

	return copy_identity(cred->id);
}

extern int auth_p_pack(auth_cred_t *cred, buf_t *buf,
		       uint16_t protocol_version)
{
	if (!buf) {
		errno = ESLURM_AUTH_BADARG;
		return SLURM_ERROR;
	}

	packstr(cred->token, buf);

	return SLURM_SUCCESS;
}

extern auth_cred_t *auth_p_unpack(buf_t *buf, uint16_t protocol_version)
{
	auth_cred_t *cred = NULL;
	uint32_t uint32_tmp;

	if (!buf) {
		errno = ESLURM_AUTH_BADARG;
		return NULL;
	}

	cred = new_cred();
	safe_unpackstr_xmalloc(&cred->token, &uint32_tmp, buf);

	return cred;

unpack_error:
	FREE_NULL_CRED(cred);
	errno = ESLURM_AUTH_UNPACK;
	return NULL;
}

extern int auth_p_thread_config(const char *token, const char *username)
{
	return ESLURM_AUTH_CRED_INVALID;
}

extern void auth_p_thread_clear(void)
{
	/* no op */
}

extern char *auth_p_token_generate(const char *username, int lifespan)
{
	return NULL;
}
