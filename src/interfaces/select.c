/*****************************************************************************\
 *  select.c - resource selection plugin wrapper.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#include "config.h"

#include <dirent.h>
#include <pthread.h>

#include "src/common/list.h"
#include "src/interfaces/select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

typedef struct {
	uint32_t	(*plugin_id);
	int		(*node_init)		(void);
	int		(*job_test)		(job_record_t *job_ptr,
						 bitstr_t *bitmap,
						 uint32_t min_nodes,
						 uint32_t max_nodes,
						 uint32_t req_nodes,
						 uint16_t mode,
						 list_t *preeemptee_candidates,
						 list_t **preemptee_job_list,
						 resv_exc_t *resv_exc_ptr,
						 will_run_data_t *will_run_ptr);
	int		(*job_begin)		(job_record_t *job_ptr);
	int		(*job_ready)		(job_record_t *job_ptr);
	int		(*job_expand)		(job_record_t *from_job_ptr,
						 job_record_t *to_job_ptr);
	int		(*job_resized)		(job_record_t *job_ptr,
						 node_record_t *node_ptr);
	int		(*job_fini)		(job_record_t *job_ptr);
	int		(*job_suspend)		(job_record_t *job_ptr,
						 bool indf_susp);
	int		(*job_resume)		(job_record_t *job_ptr,
						 bool indf_susp);
	int		(*nodeinfo_set_all)	(void);
	int		(*nodeinfo_set)		(job_record_t *job_ptr);
	int		(*reconfigure)		(void);
} slurm_select_ops_t;

static const char *node_select_syms[] = {
	"plugin_id",
	"select_p_node_init",
	"select_p_job_test",
	"select_p_job_begin",
	"select_p_job_ready",
	"select_p_job_expand",
	"select_p_job_resized",
	"select_p_job_fini",
	"select_p_job_suspend",
	"select_p_job_resume",
	"select_p_select_nodeinfo_set_all",
	"select_p_select_nodeinfo_set",
	"select_p_reconfigure",
};

static slurm_select_ops_t ops;
static plugin_context_t *select_context = NULL;
static pthread_mutex_t select_context_lock = PTHREAD_MUTEX_INITIALIZER;

extern bool running_cons_tres(void)
{
	xassert(running_in_slurmctld());

	if (*ops.plugin_id == SELECT_PLUGIN_CONS_TRES)
		return true;
	return false;
}

/*
 * Initialize context for node selection plugin
 */
extern int select_g_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "select";

	slurm_mutex_lock( &select_context_lock );

	if ( select_context )
		goto done;

	select_context =
		plugin_context_create(plugin_type, slurm_conf.select_type,
				      (void **) &ops, node_select_syms,
				      sizeof(node_select_syms));

	if (!select_context) {
		error("cannot create %s context for %s",
		      plugin_type, slurm_conf.select_type);
		retval = SLURM_ERROR;
	}

done:
	slurm_mutex_unlock( &select_context_lock );
	if (running_in_slurmctld() && !running_cons_tres()) {
		uint16_t cr_type = slurm_conf.select_type_param;
		if (cr_type & (CR_CPU | CR_CORE | CR_SOCKET)) {
			fatal("Invalid SelectTypeParameters for "
			      "%s: %s (%u), it can't contain "
			      "CR_(CPU|CORE|SOCKET).",
			      slurm_conf.select_type,
			      select_type_param_string(cr_type),
			      cr_type);
		}
	}

	return retval;
}

extern int select_g_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&select_context_lock);
	if (!select_context)
		goto fini;

	rc = plugin_context_destroy(select_context);
	select_context = NULL;

fini:	slurm_mutex_unlock(&select_context_lock);
	return rc;
}

/*
 * Convert SelectTypeParameter to equivalent string
 * NOTE: Not reentrant
 */
extern char *select_type_param_string(uint16_t select_type_param)
{
	static char select_str[1024];

	select_str[0] = '\0';
	if ((select_type_param & CR_CPU) &&
	    (select_type_param & CR_MEMORY))
		strcat(select_str, "CR_CPU_MEMORY");
	else if ((select_type_param & CR_CORE) &&
		 (select_type_param & CR_MEMORY))
		strcat(select_str, "CR_CORE_MEMORY");
	else if ((select_type_param & CR_SOCKET) &&
		 (select_type_param & CR_MEMORY))
		strcat(select_str, "CR_SOCKET_MEMORY");
	else if (select_type_param & CR_CPU)
		strcat(select_str, "CR_CPU");
	else if (select_type_param & CR_CORE)
		strcat(select_str, "CR_CORE");
	else if (select_type_param & CR_SOCKET)
		strcat(select_str, "CR_SOCKET");
	else if (select_type_param & CR_MEMORY)
		strcat(select_str, "CR_MEMORY");

	if (select_type_param & CR_ONE_TASK_PER_CORE) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "CR_ONE_TASK_PER_CORE");
	}
	if (select_type_param & CR_CORE_DEFAULT_DIST_BLOCK) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "CR_CORE_DEFAULT_DIST_BLOCK");
	}
	if (select_type_param & CR_LLN) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "CR_LLN");
	}
	if (select_type_param & CR_PACK_NODES) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "CR_PACK_NODES");
	}
	if (select_type_param & LL_SHARED_GRES) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "LL_SHARED_GRES");
	}
	if (select_type_param & MULTIPLE_SHARING_GRES_PJ) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "MULTIPLE_SHARING_GRES_PJ");
	}
	if (select_type_param & ENFORCE_BINDING_GRES) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "ENFORCE_BINDING_GRES");
	}
	if (select_type_param & ONE_TASK_PER_SHARING_GRES) {
		if (select_str[0])
			strcat(select_str, ",");
		strcat(select_str, "ONE_TASK_PER_SHARING_GRES");
	}
	if (select_str[0] == '\0')
		strcat(select_str, "NONE");

	return select_str;
}

/*
 * Note re/initialization of node record data structure
 * IN node_ptr - current node data
 * IN node_count - number of node entries
 */
extern int select_g_node_init(void)
{
	return (*(ops.node_init))();
}

/*
 * Select the "best" nodes for given job from those available
 * IN/OUT job_ptr - pointer to job being considered for initiation,
 *                  set's start_time when job expected to start
 * IN/OUT bitmap - map of nodes being considered for allocation on input,
 *                 map of nodes actually to be assigned on output
 * IN min_nodes - minimum number of nodes to allocate to job
 * IN max_nodes - maximum number of nodes to allocate to job
 * IN req_nodes - requested (or desired) count of nodes
 * IN mode - SELECT_MODE_RUN_NOW: try to schedule job now
 *           SELECT_MODE_TEST_ONLY: test if job can ever run
 *           SELECT_MODE_WILL_RUN: determine when and where job can run
 * IN preemptee_candidates - List of pointers to jobs which can bee preempted
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 *		Existing list is appended to.
 * IN resv_exc_ptr - Various TRES which the job can NOT use.
 * IN will_run_ptr - Pointer to data specific to WILL_RUN mode
 * RET SLURM_SUCCESS on success, rc otherwise
 */
extern int select_g_job_test(job_record_t *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     list_t *preemptee_candidates,
			     list_t **preemptee_job_list,
			     resv_exc_t *resv_exc_ptr,
			     will_run_data_t *will_run_ptr)
{
	return (*(ops.job_test))(job_ptr, bitmap, min_nodes, max_nodes,
				 req_nodes, mode, preemptee_candidates,
				 preemptee_job_list, resv_exc_ptr,
				 will_run_ptr);
}

/*
 * Note initiation of job is about to begin. Called immediately
 * after select_g_job_test(). Executed from slurmctld.
 * IN job_ptr - pointer to job being initiated
 */
extern int select_g_job_begin(job_record_t *job_ptr)
{
	return (*(ops.job_begin))(job_ptr);
}

/*
 * determine if job is ready to execute per the node select plugin
 * IN job_ptr - pointer to job being tested
 * RET: -2 fatal error, -1 try again, 1 if ready to execute,
 *	0 not ready to execute
 */
extern int select_g_job_ready(job_record_t *job_ptr)
{
	return (*(ops.job_ready))(job_ptr);
}

/*
 * Move the resource allocated to one job into that of another job.
 *	All resources are removed from "from_job_ptr" and moved into
 *	"to_job_ptr". Also see other_job_resized().
 * RET: 0 or an error code
 */
extern int select_g_job_expand(job_record_t *from_job_ptr,
			       job_record_t *to_job_ptr)
{
	return (*(ops.job_expand))(from_job_ptr, to_job_ptr);
}

/*
 * Modify internal data structures for a job that has changed size
 *	Only support jobs shrinking now.
 * RET: 0 or an error code
 */
extern int select_g_job_resized(job_record_t *job_ptr, node_record_t *node_ptr)
{
	return (*(ops.job_resized))(job_ptr, node_ptr);
}

/*
 * Note termination of job is starting. Executed from slurmctld.
 * IN job_ptr - pointer to job being terminated
 */
extern int select_g_job_fini(job_record_t *job_ptr)
{
	return (*(ops.job_fini))(job_ptr);
}

/*
 * Suspend a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being suspended
 * IN indf_susp - set if job is being suspended indefinitely by user
 *                or admin, otherwise suspended for gang scheduling
 * RET SLURM_SUCCESS or error code
 */
extern int select_g_job_suspend(job_record_t *job_ptr, bool indf_susp)
{
	return (*(ops.job_suspend))(job_ptr, indf_susp);
}

/*
 * Resume a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being resumed
 * IN indf_susp - set if job is being resumed from indefinite suspend by user
 *                or admin, otherwise resume from gang scheduling
 * RET SLURM_SUCCESS or error code
 */
extern int select_g_job_resume(job_record_t *job_ptr, bool indf_susp)
{
	return (*(ops.job_resume))(job_ptr, indf_susp);
}

extern int select_g_select_nodeinfo_set_all(void)
{
	return (*(ops.nodeinfo_set_all))();
}

extern int select_g_select_nodeinfo_set(job_record_t *job_ptr)
{
	return (*(ops.nodeinfo_set))(job_ptr);
}

/*
 * packs the select plugin_id for backwards compatibility
 * Remove when 24.11 is no longer supported.
 */
extern void select_plugin_id_pack(buf_t *buffer)
{
	pack32(*(ops.plugin_id), buffer);
}

/*
 * Note reconfiguration or change in partition configuration
 */
extern int select_g_reconfigure (void)
{
	return (*(ops.reconfigure))();
}
