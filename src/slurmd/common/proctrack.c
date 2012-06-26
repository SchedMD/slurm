/*****************************************************************************\
 *  proctrack.c - Process tracking plugin stub.
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "src/common/log.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/* ************************************************************************ */
/*  TAG(                        slurm_proctrack_ops_t                    )  */
/* ************************************************************************ */
typedef struct slurm_proctrack_ops {
	int              (*create)    (slurmd_job_t * job);
	int              (*add)       (slurmd_job_t * job, pid_t pid);
	int              (*signal)    (uint64_t id, int signal);
	int              (*destroy)   (uint64_t id);
	uint64_t         (*find_cont) (pid_t pid);
	bool             (*has_pid)   (uint64_t id, pid_t pid);
	int              (*wait)      (uint64_t id);
	int              (*get_pids)  (uint64_t id, pid_t ** pids, int *npids);
} slurm_proctrack_ops_t;

/*
 * Must be synchronized with slurm_proctrack_ops_t above.
 */
static const char *syms[] = {
	"slurm_container_plugin_create",
	"slurm_container_plugin_add",
	"slurm_container_plugin_signal",
	"slurm_container_plugin_destroy",
	"slurm_container_plugin_find",
	"slurm_container_plugin_has_pid",
	"slurm_container_plugin_wait",
	"slurm_container_plugin_get_pids"
};

static slurm_proctrack_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

/* *********************************************************************** */
/*  TAG(                    slurm_proctrack_init                        )  */
/*                                                                         */
/*  NOTE: The proctrack plugin can only be changed by restarting slurmd    */
/*        without preserving state (-c option).                            */
/* *********************************************************************** */
extern int slurm_proctrack_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "proctrack";
	char *type = NULL;

	if (init_run && g_context)
		return retval;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	type = slurm_get_proctrack_type();
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

/* *********************************************************************** */
/*  TAG(                    slurm_proctrack_fini                        )  */
/* *********************************************************************** */
extern int slurm_proctrack_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;
	return rc;
}

/*
 * Create a container
 * job IN - slurmd_job_t structure
 * job->cont_id OUT - Plugin must fill in job->cont_id either here
 *                    or in slurm_container_add()
 *
 * Returns a SLURM errno.
 */
extern int slurm_container_create(slurmd_job_t * job)
{
	if (slurm_proctrack_init() < 0)
		return 0;

	return (*(ops.create)) (job);
}

/*
 * Add a process to the specified container
 * job IN - slurmd_job_t structure
 * pid IN      - process ID to be added to the container
 * job->cont_id OUT - Plugin must fill in job->cont_id either here
 *                    or in slurm_container_create()
 *
 * Returns a SLURM errno.
 */
extern int slurm_container_add(slurmd_job_t * job, pid_t pid)
{
	if (slurm_proctrack_init() < 0)
		return SLURM_ERROR;

	return (*(ops.add)) (job, pid);
}

/*
 * Signal all processes within a container
 * cont_id IN - container ID as returned by slurm_container_create()
 * signal IN  - signal to send, if zero then perform error checking
 *              but do not send signal
 *
 * Returns a SLURM errno.
 */
extern int slurm_container_signal(uint64_t cont_id, int signal)
{
	if (slurm_proctrack_init() < 0) {
		return SLURM_ERROR;
	}
	return (*(ops.signal)) (cont_id, signal);
}

/*
 * Destroy a container, any processes within the container are not effected
 * cont_id IN - container ID as returned by slurm_container_create()
 *
 * Returns a SLURM errno.
*/
extern int slurm_container_destroy(uint64_t cont_id)
{
	if (slurm_proctrack_init() < 0)
		return SLURM_ERROR;

	return (*(ops.destroy)) (cont_id);
}

/*
 * Get container ID for given process ID
 *
 * Returns zero if no container found for the given pid.
 */
extern uint64_t slurm_container_find(pid_t pid)
{
	if (slurm_proctrack_init() < 0)
		return SLURM_ERROR;

	return (*(ops.find_cont)) (pid);
}

/*
 * Return "true" if the container "cont_id" contains the process with
 * ID "pid".
 */
extern bool slurm_container_has_pid(uint64_t cont_id, pid_t pid)
{
	if (slurm_proctrack_init() < 0)
		return SLURM_ERROR;

	return (*(ops.has_pid)) (cont_id, pid);
}

/*
 * Wait for all processes within a container to exit.
 *
 * When slurm_container_wait returns SLURM_SUCCESS, the container is considered
 * destroyed.  There is no need to call slurm_container_destroy after
 * a successful call to slurm_container_wait, and in fact it will trigger
 * undefined behavior.
 *
 * Return SLURM_SUCCESS or SLURM_ERROR.
 */
extern int slurm_container_wait(uint64_t cont_id)
{
	if (slurm_proctrack_init() < 0)
		return SLURM_ERROR;

	return (*(ops.wait)) (cont_id);
}

/*
 * Get all process IDs within a container.
 *
 * IN cont_id - Container ID.
 * OUT pids - a pointer to an xmalloc'ed array of process ids, of
 *	length "npids".  Caller must free array with xfree().
 * OUT npids - number of process IDs in the returned "pids" array.
 *
 * Return SLURM_SUCCESS if container exists (npids may be zero, and
 *   pids NULL), return SLURM_ERROR if container does not exist, or
 *   plugin does not implement the call.
 */
extern int
slurm_container_get_pids(uint64_t cont_id, pid_t ** pids, int *npids)
{
	if (slurm_proctrack_init() < 0)
		return SLURM_ERROR;

	return (*(ops.get_pids)) (cont_id, pids, npids);
}
