/*****************************************************************************\
 *  other_select.c - node selection plugin wrapper for other select plugins.
 *
 *  NOTE: The node selection plugin itself is intimately tied to slurmctld
 *  functions and data structures. Some related functions (e.g. data structure
 *  un/packing, environment variable setting) are required by most Slurm
 *  commands. Since some of these commands must be executed on the BlueGene
 *  front-end nodes, the functions they require are here rather than within
 *  the plugin. This is because functions required by the plugin can not be
 *  resolved on the front-end nodes, so we can't load the plugins there.
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

#include <dirent.h>
#include <pthread.h>

#include "other_select.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"

uint16_t other_select_type_param = 0;

/*
 * Must be synchronized with slurm_select_ops_t in select.h.
 */
const char *node_select_syms[] = {
	"plugin_id",
	"select_p_state_save",
	"select_p_state_restore",
	"select_p_job_init",
	"select_p_node_init",
	"select_p_job_test",
	"select_p_job_begin",
	"select_p_job_ready",
	"select_p_job_expand",
	"select_p_job_resized",
	"select_p_job_signal",
	"select_p_job_fini",
	"select_p_job_suspend",
	"select_p_job_resume",
	"select_p_step_pick_nodes",
	"select_p_step_start",
	"select_p_step_finish",
	"select_p_select_nodeinfo_pack",
	"select_p_select_nodeinfo_unpack",
	"select_p_select_nodeinfo_alloc",
	"select_p_select_nodeinfo_free",
	"select_p_select_nodeinfo_set_all",
	"select_p_select_nodeinfo_set",
	"select_p_select_nodeinfo_get",
	"select_p_select_jobinfo_alloc",
	"select_p_select_jobinfo_free",
	"select_p_select_jobinfo_set",
	"select_p_select_jobinfo_get",
	"select_p_select_jobinfo_copy",
	"select_p_select_jobinfo_pack",
	"select_p_select_jobinfo_unpack",
	"select_p_select_jobinfo_sprint",
	"select_p_select_jobinfo_xstrdup",
	"select_p_get_info_from_plugin",
	"select_p_update_node_config",
	"select_p_reconfigure",
	"select_p_resv_test",
};

static slurm_select_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t	g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

/*
 * Initialize context for node selection plugin
 */
extern int other_select_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "select";
	char *type = NULL;
	int n_syms;

	if (init_run && g_context)
		return retval;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	if (!other_select_type_param)
		other_select_type_param = slurm_conf.select_type_param;

	if (other_select_type_param & CR_OTHER_CONS_RES)
		type = "select/cons_res";
	else if (other_select_type_param & CR_OTHER_CONS_TRES)
		type = "select/cons_tres";
	else
		type = "select/linear";

	n_syms = sizeof(node_select_syms);
	if (n_syms != sizeof(ops))
		fatal("For some reason node_select_syms in "
		      "src/plugins/select/other/other_select.c differs from "
		      "slurm_select_ops_t found in src/common/select.h.  "
		      "node_select_syms should match what is in "
		      "src/common/node_select.c");

	if (!(g_context = plugin_context_create(
		     plugin_type, type, (void **)&ops,
		     node_select_syms, n_syms))) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	slurm_mutex_unlock(&g_context_lock);
	return retval;
}

extern int other_select_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	init_run = false;
	if (!g_context)
		goto fini;

	rc = plugin_context_destroy(g_context);
	g_context = NULL;
fini:
	slurm_mutex_unlock(&g_context_lock);
	return rc;
}

/*
 * Save any global state information
 * IN dir_name - directory into which the data can be stored
 */
extern int other_state_save(char *dir_name)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.state_save))(dir_name);
}

/*
 * Initialize context for node selection plugin and
 * restore any global state information
 * IN dir_name - directory from which the data can be restored
 */
extern int other_state_restore(char *dir_name)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.state_restore))(dir_name);
}

/*
 * Note the initialization of job records, issued upon restart of
 * slurmctld and used to synchronize any job state.
 */
extern int other_job_init(List job_list)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.job_init))(job_list);
}

/*
 * Note re/initialization of node record data structure
 * IN node_ptr - current node data
 * IN node_count - number of node entries
 */
extern int other_node_init()
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

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
 * IN exc_core_bitmap - bitmap of cores being reserved.
 * RET zero on success, EINVAL otherwise
 */
extern int other_job_test(job_record_t *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  uint32_t req_nodes, uint16_t mode,
			  List preemptee_candidates, List *preemptee_job_list,
			  bitstr_t *exc_core_bitmap)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.job_test))
		(job_ptr, bitmap,
		 min_nodes, max_nodes,
		 req_nodes, mode,
		 preemptee_candidates, preemptee_job_list,
		 exc_core_bitmap);
}

/*
 * Note initiation of job is about to begin. Called immediately
 * after other_job_test(). Executed from slurmctld.
 * IN job_ptr - pointer to job being initiated
 */
extern int other_job_begin(job_record_t *job_ptr)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.job_begin))(job_ptr);
}

/*
 * determine if job is ready to execute per the node select plugin
 * IN job_ptr - pointer to job being tested
 * RET: -2 fatal error, -1 try again, 1 if ready to execute,
 *	0 not ready to execute
 */
extern int other_job_ready(job_record_t *job_ptr)
{
	if (other_select_init() < 0)
		return -1;

	return (*(ops.job_ready))(job_ptr);
}

/*
 * Move the resource allocated to one job into that of another job.
 *	All resources are removed from "from_job_ptr" and moved into
 *	"to_job_ptr". Also see other_job_resized().
 * RET: 0 or an error code
 */
extern int other_job_expand(job_record_t *from_job_ptr,
			    job_record_t *to_job_ptr)
{
	if (other_select_init() < 0)
		return -1;

	return (*(ops.job_expand))(from_job_ptr, to_job_ptr);
}

/*
 * Modify internal data structures for a job that has decreased job size.
 *	Only support jobs shrinking. Also see other_job_expand();
 * RET: 0 or an error code
 */
extern int other_job_resized(job_record_t *job_ptr, node_record_t *node_ptr)
{
	if (other_select_init() < 0)
		return -1;

	return (*(ops.job_resized))(job_ptr, node_ptr);
}

/*
 * Pass job-step signal to other plugin.
 * IN job_ptr - job to be signaled
 * IN signal  - signal(7) number
 */
extern int other_job_signal(job_record_t *job_ptr, int signal)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.job_signal))(job_ptr, signal);
}

/*
 * Note termination of job is starting. Executed from slurmctld.
 * IN job_ptr - pointer to job being terminated
 */
extern int other_job_fini(job_record_t *job_ptr)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.job_fini))(job_ptr);
}

/*
 * Suspend a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being suspended
 * indf_susp IN - set if job is being suspended indefinitely by user
 *                or admin, otherwise suspended for gang scheduling
 * RET SLURM_SUCCESS or error code
 */
extern int other_job_suspend(job_record_t *job_ptr, bool indf_susp)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.job_suspend))(job_ptr, indf_susp);
}

/*
 * Resume a job. Executed from slurmctld.
 * indf_susp IN - set if job is being resumed from indefinite suspend by user
 *                or admin, otherwise resume from gang scheduling
 * IN job_ptr - pointer to job being resumed
 * RET SLURM_SUCCESS or error code
 */
extern int other_job_resume(job_record_t *job_ptr, bool indf_susp)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.job_resume))(job_ptr, indf_susp);
}

/*
 * Select the "best" nodes for given job step from those available in
 * a job allocation.
 *
 * IN/OUT job_ptr - pointer to job already allocated and running in a
 *                  block where the step is to run.
 *                  set's start_time when job expected to start
 * OUT step_jobinfo - Fill in the resources to be used if not
 *                    full size of job.
 * IN node_count  - How many nodes we are looking for.
 * OUT avail_nodes - bitmap of available nodes according to the plugin
 *                  (not always set).
 * RET map of slurm nodes to be used for step, NULL on failure
 */
extern bitstr_t *other_step_pick_nodes(job_record_t *job_ptr,
				       select_jobinfo_t *jobinfo,
				       uint32_t node_count,
				       bitstr_t **avail_nodes)
{
	if (other_select_init() < 0)
		return NULL;

	return (*(ops.step_pick_nodes))(job_ptr, jobinfo, node_count,
					avail_nodes);
}

extern int other_step_start(step_record_t *step_ptr)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.step_start))
		(step_ptr);
}

/*
 * clear what happened in select_g_step_pick_nodes
 * IN/OUT step_ptr - Flush the resources from the job and step.
 * IN killing_step - if true then we are just starting to kill the step
 *                   if false, the step is completely terminated
 */
extern int other_step_finish(step_record_t *step_ptr, bool killing_step)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.step_finish))
		(step_ptr, killing_step);
}

extern int other_select_nodeinfo_pack(select_nodeinfo_t *nodeinfo,
				      buf_t *buffer,
				      uint16_t protocol_version)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.nodeinfo_pack))(nodeinfo, buffer, protocol_version);
}

extern int other_select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo,
					buf_t *buffer,
					uint16_t protocol_version)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.nodeinfo_unpack))(nodeinfo, buffer, protocol_version);
}

extern select_nodeinfo_t *other_select_nodeinfo_alloc(void)
{
	if (other_select_init() < 0)
		return NULL;

	return (*(ops.nodeinfo_alloc))();
}

extern int other_select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.nodeinfo_free))(nodeinfo);
}

extern int other_select_nodeinfo_set_all(void)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.nodeinfo_set_all))();
}

extern int other_select_nodeinfo_set(job_record_t *job_ptr)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.nodeinfo_set))(job_ptr);
}

extern int other_select_nodeinfo_get(select_nodeinfo_t *nodeinfo,
					enum select_nodedata_type dinfo,
					enum node_states state,
					void *data)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.nodeinfo_get))(nodeinfo, dinfo, state, data);
}

extern select_jobinfo_t *other_select_jobinfo_alloc(void)
{
	if (other_select_init() < 0)
		return NULL;

	return (*(ops.jobinfo_alloc))();;
}

/* free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 */
extern int other_select_jobinfo_free(select_jobinfo_t *jobinfo)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;
	return (*(ops.jobinfo_free))(jobinfo);
}

extern int other_select_jobinfo_set(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.jobinfo_set))(jobinfo, data_type, data);
}

/* get data from a select job credential
 * IN jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN/OUT data - the data to enter into job credential
 */
extern int other_select_jobinfo_get(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.jobinfo_get))(jobinfo, data_type, data);
}

/* copy a select job credential
 * IN jobinfo - the select job credential to be copied
 * RET        - the copy or NULL on failure
 * NOTE: returned value must be freed using other_free_jobinfo
 */
extern select_jobinfo_t *other_select_jobinfo_copy(
	select_jobinfo_t *jobinfo)
{
	if (other_select_init() < 0)
		return NULL;

	return (*(ops.jobinfo_copy))(jobinfo);
}

/* pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * RET         - slurm error code
 */
extern int other_select_jobinfo_pack(select_jobinfo_t *jobinfo,
				     buf_t *buffer,
				     uint16_t protocol_version)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.jobinfo_pack))(jobinfo, buffer, protocol_version);
}

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * RET         - slurm error code
 * NOTE: returned value must be freed using other_free_jobinfo
 */
extern int other_select_jobinfo_unpack(select_jobinfo_t **jobinfo,
				       buf_t *buffer,
				       uint16_t protocol_version)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.jobinfo_unpack))(jobinfo, buffer, protocol_version);
}

/* write select job credential to a string
 * IN jobinfo - a select job credential
 * OUT buf    - location to write job credential contents
 * IN size    - byte size of buf
 * IN mode    - print mode, see enum select_print_mode
 * RET        - the string, same as buf
 */
extern char *other_select_jobinfo_sprint(select_jobinfo_t *jobinfo,
					    char *buf, size_t size, int mode)
{
	if (other_select_init() < 0)
		return NULL;

	return (*(ops.jobinfo_sprint))(jobinfo, buf, size, mode);
}
/* write select job info to a string
 * IN jobinfo - a select job credential
 * IN mode    - print mode, see enum select_print_mode
 * RET        - char * containing string of request
 */
extern char *other_select_jobinfo_xstrdup(
	select_jobinfo_t *jobinfo, int mode)
{
	if (other_select_init() < 0)
		return NULL;

	return (*(ops.jobinfo_xstrdup))(jobinfo, mode);
}

/*
 * Get select data from a plugin
 * IN dinfo     - type of data to get from the node record
 *                (see enum select_plugindata_info)
 * IN/OUT data  - the data to get from node record
 */
extern int other_get_info_from_plugin(enum select_plugindata_info dinfo,
				      job_record_t *job_ptr, void *data)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.get_info_from_plugin))(dinfo, job_ptr, data);
}

/*
 * Updated a node configuration. This happens when a node registers with
 *     more resources than originally configured (e.g. memory).
 * IN index  - index into the node record list
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int other_update_node_config (int index)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.
		  update_node_config))(index);
}

/*
 * Note reconfiguration or change in partition configuration
 */
extern int other_reconfigure (void)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(ops.reconfigure))();
}

extern bitstr_t * other_resv_test(resv_desc_msg_t *resv_desc_ptr,
				  uint32_t node_cnt,
				  bitstr_t *avail_bitmap,
				  bitstr_t **core_bitmap)
{
	if (other_select_init() < 0)
		return NULL;

	return (*(ops.resv_test))(resv_desc_ptr, node_cnt,
				  avail_bitmap, core_bitmap);
}
