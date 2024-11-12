/*****************************************************************************\
 *  certmgr.c - certmgr API definitions
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

#include "src/interfaces/certmgr.h"

typedef struct {
	char *(*get_node_token)(char *node_name);
	char *(*generate_csr)(char *node_name);
	char *(*sign_csr)(char *csr, char *token, node_record_t *node);
} certmgr_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for certmgr_ops_t.
 */
static const char *syms[] = {
	"certmgr_p_get_node_token",
	"certmgr_p_generate_csr",
	"certmgr_p_sign_csr",
};

static certmgr_ops_t ops;
static plugin_context_t *g_context = NULL;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static pthread_rwlock_t context_lock = PTHREAD_RWLOCK_INITIALIZER;

extern bool certmgr_enabled(void)
{
	return (plugin_inited == PLUGIN_INITED);
}

extern int certmgr_get_renewal_period_mins(void)
{
	static time_t renewal_period = NO_VAL;
	char *renewal_str = NULL;

	if (renewal_period != NO_VAL)
		return renewal_period;

	if ((renewal_str = conf_get_opt_str(slurm_conf.certmgr_params,
					    "certificate_renewal_period="))) {
		int i = atoi(renewal_str);
		if (i < 0) {
			error("Invalid certificate_renewal_period: %s. Needs to be positive integer",
			      renewal_str);
			xfree(renewal_str);
			return SLURM_ERROR;
		}

		renewal_period = i;
		xfree(renewal_str);
		return renewal_period;
	} else {
		/* default setting */
		renewal_period = DAY_MINUTES;
	}

	return renewal_period;
}

extern int certmgr_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *plugin_type = "certmgr";

	if (!running_in_slurmctld() && !running_in_slurmd()) {
		error("certmgr plugin only allowed on slurmctld and slurmd");
		return SLURM_ERROR;
	}

	slurm_rwlock_wrlock(&context_lock);

	if (plugin_inited != PLUGIN_NOT_INITED)
		goto done;

	if (!slurm_conf.certmgr_type) {
		plugin_inited = PLUGIN_NOOP;
		goto done;
	}

	g_context = plugin_context_create(plugin_type, slurm_conf.certmgr_type,
					  (void **) &ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s",
		      plugin_type, slurm_conf.certmgr_type);
		rc = SLURM_ERROR;
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}

	if (certmgr_get_renewal_period_mins() == SLURM_ERROR) {
		rc = SLURM_ERROR;
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}

	plugin_inited = PLUGIN_INITED;

done:
	slurm_rwlock_unlock(&context_lock);
	return rc;
}

extern int certmgr_g_fini(void)
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

extern char *certmgr_g_get_node_token(char *node_name)
{
	xassert(running_in_slurmd());
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return SLURM_SUCCESS;

	return (*(ops.get_node_token))(node_name);
}

extern char *certmgr_g_generate_csr(char *node_name)
{
	xassert(running_in_slurmd());
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return SLURM_SUCCESS;

	return (*(ops.generate_csr))(node_name);
}

extern char *certmgr_g_sign_csr(char *csr, char *token, node_record_t *node)
{
	xassert(running_in_slurmctld());
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return SLURM_SUCCESS;

	return (*(ops.sign_csr))(csr, token, node);
}
