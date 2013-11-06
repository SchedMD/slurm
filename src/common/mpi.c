/*****************************************************************************\
 * src/common/mpi.c - Generic mpi selector for slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondo1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/env.h"
#include "src/common/mpi.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, MPI plugins
 * will stop working.  If you need to add fields, add them
 * at the end of the structure.
 */
typedef struct slurm_mpi_ops {
	int          (*slurmstepd_prefork)(const stepd_step_rec_t *job,
					   char ***env);
	int          (*slurmstepd_init)   (const mpi_plugin_task_info_t *job,
					   char ***env);
	mpi_plugin_client_state_t *
	             (*client_prelaunch)  (const mpi_plugin_client_info_t *job,
					   char ***env);
	bool         (*client_single_task)(void);
	int          (*client_fini)       (mpi_plugin_client_state_t *);
} slurm_mpi_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_mpi_ops_t.
 */
static const char *syms[] = {
	"p_mpi_hook_slurmstepd_prefork",
	"p_mpi_hook_slurmstepd_task",
	"p_mpi_hook_client_prelaunch",
	"p_mpi_hook_client_single_task_per_node",
	"p_mpi_hook_client_fini"
};

static slurm_mpi_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t      context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

int _mpi_init (char *mpi_type)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "mpi";
	char *type = NULL;
	int got_default = 0;

	if (init_run && g_context)
		return retval;

	slurm_mutex_lock( &context_lock );

	if ( g_context )
		goto done;

	if (mpi_type == NULL) {
		mpi_type = slurm_get_mpi_default();
		got_default = 1;
	}
	if (mpi_type == NULL) {
		error("No MPI default set.");
		retval = SLURM_ERROR;
		goto done;
	}

	if (!strcmp(mpi_type, "list")) {
		char *plugin_dir;
		plugrack_t mpi_rack;

		mpi_rack = plugrack_create();
		if (!mpi_rack) {
			error("Unable to create a plugin manager");
			exit(0);
		}
		plugrack_set_major_type(mpi_rack, "mpi");
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir(mpi_rack, plugin_dir);
		plugrack_print_all_plugin(mpi_rack);
		exit(0);
	}

	setenvf(NULL, "SLURM_MPI_TYPE", "%s", mpi_type);

	type = xstrdup_printf("mpi/%s", mpi_type);

	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	xfree(type);
	if (got_default)
		xfree(mpi_type);
	slurm_mutex_unlock( &context_lock );
	return retval;
}

int mpi_hook_slurmstepd_init (char ***env)
{
	char *mpi_type = getenvp (*env, "SLURM_MPI_TYPE");

	debug("mpi type = %s", mpi_type);

	if (_mpi_init(mpi_type) == SLURM_ERROR)
		return SLURM_ERROR;

	unsetenvp (*env, "SLURM_MPI_TYPE");

	return SLURM_SUCCESS;
}

int mpi_hook_slurmstepd_prefork (const stepd_step_rec_t *job, char ***env)
{
	if (mpi_hook_slurmstepd_init(env) == SLURM_ERROR)
		return SLURM_ERROR;

	return (*(ops.slurmstepd_prefork))(job, env);
}

int mpi_hook_slurmstepd_task (const mpi_plugin_task_info_t *job, char ***env)
{
	if (mpi_hook_slurmstepd_init(env) == SLURM_ERROR)
		return SLURM_ERROR;

	return (*(ops.slurmstepd_init))(job, env);
}

int mpi_hook_client_init (char *mpi_type)
{
	debug("mpi type = %s", mpi_type);

	if (_mpi_init(mpi_type) == SLURM_ERROR)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

mpi_plugin_client_state_t *
mpi_hook_client_prelaunch(const mpi_plugin_client_info_t *job, char ***env)
{
	if (_mpi_init(NULL) < 0)
		return NULL;

	return (*(ops.client_prelaunch))(job, env);
}

bool mpi_hook_client_single_task_per_node (void)
{
	if (_mpi_init(NULL) < 0)
		return SLURM_ERROR;
#if defined HAVE_BGQ
//#if defined HAVE_BGQ && defined HAVE_BG_FILES
	/* On BGQ systems we only want 1 task to be spawned since srun
	   is wrapping runjob.
	*/
	return true;
#else
	return (*(ops.client_single_task))();
#endif
}

int mpi_hook_client_fini (mpi_plugin_client_state_t *state)
{
	if (_mpi_init(NULL) < 0)
		return SLURM_ERROR;

	return (*(ops.client_fini))(state);
}

int mpi_fini (void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;
	rc = plugin_context_destroy(g_context);
	return rc;
}


