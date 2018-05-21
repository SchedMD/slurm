/*****************************************************************************\
 *  sched_plugin.c - scheduler plugin stub.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>.
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

#include "src/common/log.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/gang.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"

typedef struct slurm_sched_ops {
	uint32_t	(*initial_priority)	( uint32_t,
						  struct job_record * );
	int		(*reconfig)		( void );
} slurm_sched_ops_t;

/*
 * Must be synchronized with slurm_sched_ops_t above.
 */
static const char *syms[] = {
	"slurm_sched_p_initial_priority",
	"slurm_sched_p_reconfig",
};

static slurm_sched_ops_t ops;
static plugin_context_t	*g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

/*
 * The scheduler plugin can not be changed via reconfiguration
 * due to background threads, job priorities, etc.
 * slurmctld must be restarted and job priority changes may be
 * required to change the scheduler type.
 */
extern int slurm_sched_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "sched";
	char *type = NULL;

	if ( init_run && g_context )
		return retval;

	slurm_mutex_lock( &g_context_lock );

	if ( g_context )
		goto done;

	type = slurm_get_sched_type();
	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	slurm_mutex_unlock( &g_context_lock );
	xfree(type);
	return retval;
}

extern int slurm_sched_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;

	gs_fini();

	return rc;
}

extern int slurm_sched_g_reconfig(void)
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

	gs_reconfig();

	return (*(ops.reconfig))();
}

uint32_t slurm_sched_g_initial_priority(uint32_t last_prio,
					struct job_record *job_ptr)
{
	if ( slurm_sched_init() < 0 )
		return SLURM_ERROR;

	return (*(ops.initial_priority))( last_prio, job_ptr );
}
