/*****************************************************************************\
 *  slurm_acct_gather_energy.c - implementation-independent job energy
 *  accounting plugin definitions
 *****************************************************************************
 *  Copyright (C) 2012 Bull-HN-PHX.
 *  Written by Bull-HN-PHX/d.rusak,
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

typedef struct slurm_acct_gather_energy_ops {
	int (*update_node_energy) (void);
	int (*get_data)           (enum acct_energy_type data_type,
				   acct_gather_energy_t *energy);
	int (*set_data)           (enum acct_energy_type data_type,
				   acct_gather_energy_t *energy);
} slurm_acct_gather_energy_ops_t;
/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_acct_gather_energy_ops_t.
 */
static const char *syms[] = {
	"acct_gather_energy_p_update_node_energy",
	"acct_gather_energy_p_get_data",
	"acct_gather_energy_p_set_data",
};

static slurm_acct_gather_energy_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock =	PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

extern int slurm_acct_gather_energy_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "acct_gather_energy";
	char *type = NULL;

	if (init_run && g_context)
		return retval;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	type = slurm_get_acct_gather_energy_type();

	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	slurm_mutex_unlock(&g_context_lock);
	xfree(type);

	return retval;
}

extern int acct_gather_energy_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;

	return rc;
}

extern acct_gather_energy_t *acct_gather_energy_alloc(void)
{
	acct_gather_energy_t *energy =
		xmalloc(sizeof(struct acct_gather_energy));

	return energy;
}

extern void acct_gather_energy_destroy(acct_gather_energy_t *energy)
{
	xfree(energy);
}

extern void acct_gather_energy_pack(acct_gather_energy_t *energy, Buf buffer,
				    uint16_t protocol_version)
{
	if (!energy) {
		int i;
		for (i=0; i<4; i++)
			pack32(0, buffer);
		return;
	}

	pack32(energy->base_consumed_energy, buffer);
	pack32(energy->base_watts, buffer);
	pack32(energy->consumed_energy, buffer);
	pack32(energy->current_watts, buffer);
}

extern int acct_gather_energy_unpack(acct_gather_energy_t **energy, Buf buffer,
				     uint16_t protocol_version)
{
	acct_gather_energy_t *energy_ptr = acct_gather_energy_alloc();
	*energy = energy_ptr;

	safe_unpack32(&energy_ptr->base_consumed_energy, buffer);
	safe_unpack32(&energy_ptr->base_watts, buffer);
	safe_unpack32(&energy_ptr->consumed_energy, buffer);
	safe_unpack32(&energy_ptr->current_watts, buffer);

	return SLURM_SUCCESS;

unpack_error:
	acct_gather_energy_destroy(energy_ptr);
	*energy = NULL;
	return SLURM_ERROR;
}

extern int acct_gather_energy_g_update_node_energy(void)
{
	int retval = SLURM_ERROR;

	if (slurm_acct_gather_energy_init() < 0)
		return retval;

	retval = (*(ops.update_node_energy))();

	return retval;
}

extern int acct_gather_energy_g_get_data(enum acct_energy_type data_type,
					 acct_gather_energy_t *energy)
{
	int retval = SLURM_ERROR;

	if (slurm_acct_gather_energy_init() < 0)
		return retval;

	retval = (*(ops.get_data))(data_type, energy);

	return retval;
}

extern int acct_gather_energy_g_set_data(enum acct_energy_type data_type,
					 acct_gather_energy_t *energy)
{
	int retval = SLURM_ERROR;

	if (slurm_acct_gather_energy_init() < 0)
		return retval;

	retval = (*(ops.set_data))(data_type, energy);

	return retval;
}
