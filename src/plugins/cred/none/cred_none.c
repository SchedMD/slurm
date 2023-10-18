/*****************************************************************************\
 *  cred_none.c - null job credential signature plugin
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC.
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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/plugins/cred/common/cred_common.h"

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
const char plugin_name[]	= "Null credential signature plugin";
const char plugin_type[]	= "cred/none";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

enum {
        ESIG_INVALID = 5000,
};

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is unloaded,
 * free any global memory allocations here to avoid memory leaks.
 */
extern int fini(void)
{
	verbose("%s unloaded", plugin_name);
	return SLURM_SUCCESS;
}

extern const char *cred_p_str_error(int errnum)
{
	if (errnum == ESIG_INVALID)
		return "Invalid signature";
	return NULL;
}

extern char *cred_p_sign(buf_t *buffer)
{
	return xstrdup("fake signature");
}

extern int cred_p_verify_sign(char *buffer, uint32_t buf_size, char *signature)
{
	char *correct_signature = "fake signature";
	if (xstrcmp(signature, correct_signature))
		return ESIG_INVALID;
	return SLURM_SUCCESS;
}

extern slurm_cred_t *cred_p_create(slurm_cred_arg_t *cred_arg, bool sign_it,
				   uint16_t protocol_version)
{
	slurm_cred_t *cred = cred_create(cred_arg, protocol_version);
	if (sign_it)
		packstr("fake signature", cred->buffer);
	else
		packnull(cred->buffer);
	return cred;
}

extern slurm_cred_t *cred_p_unpack(buf_t *buf, uint16_t protocol_version)
{
	slurm_cred_t *credential = NULL;

	if (!(credential = cred_unpack_with_signature(buf, protocol_version)))
		return NULL;

	/* why bother checking? */
	credential->verified = true;
	return credential;
}

extern char *cred_p_create_net_cred(void *addrs, uint16_t protocol_version)
{
	return NULL;
}

extern void *cred_p_extract_net_cred(char *net_cred, uint16_t protocol_version)
{
	return NULL;
}
