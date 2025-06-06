/*****************************************************************************\
 *  certgen.c - certgen API definitions
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

#include "src/interfaces/certgen.h"

/* Define slurm-specific aliases for use by plugins, see slurm_xlator.h. */
strong_alias(certgen_g_init, slurm_certgen_g_init);
strong_alias(certgen_g_fini, slurm_certgen_g_fini);
strong_alias(certgen_g_self_signed, slurm_certgen_g_self_signed);

typedef struct {
	int (*gen_self_signed)(char **cert_pem, char **key_pem);
} certgen_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for certgen_ops_t.
 */
static const char *syms[] = {
	"certgen_p_self_signed",
};

static certgen_ops_t ops;
static plugin_context_t *g_context = NULL;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static pthread_rwlock_t context_lock = PTHREAD_RWLOCK_INITIALIZER;

extern int certgen_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *plugin_type = "certgen";
	char *plugin = slurm_conf.certgen_type;

	slurm_rwlock_wrlock(&context_lock);

	if (plugin_inited != PLUGIN_NOT_INITED)
		goto done;

	if (!plugin)
		plugin = DEFAULT_CERTGEN_TYPE;

	g_context = plugin_context_create(plugin_type, plugin, (void **) &ops,
					  syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s",
		      plugin_type, plugin);
		rc = SLURM_ERROR;
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}

	plugin_inited = PLUGIN_INITED;

done:
	slurm_rwlock_unlock(&context_lock);
	return rc;
}

extern int certgen_g_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_rwlock_wrlock(&context_lock);

	if (g_context) {
		rc = plugin_context_destroy(g_context);
		g_context = NULL;
	}

	plugin_inited = PLUGIN_NOT_INITED;

	slurm_rwlock_unlock(&context_lock);

	return rc;
}

extern int certgen_g_self_signed(char **cert_pem, char **key_pem)
{
	int rc = SLURM_SUCCESS;

	slurm_rwlock_rdlock(&context_lock);
	xassert(plugin_inited != PLUGIN_NOT_INITED);
	rc = (*(ops.gen_self_signed))(cert_pem, key_pem);
	slurm_rwlock_unlock(&context_lock);

	return rc;
}
