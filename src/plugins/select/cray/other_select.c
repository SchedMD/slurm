/*****************************************************************************\
 *  other_select.c - node selection plugin wrapper for Cray.
 *
 *  NOTE: The node selection plugin itself is intimately tied to slurmctld
 *  functions and data structures. Some related functions (e.g. data structure
 *  un/packing, environment variable setting) are required by most SLURM
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
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <pthread.h>
#include <dirent.h>

#include "other_select.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/common/node_select.h"

/* If there is a new select plugin, list it here */
static slurm_select_context_t *other_select_context = NULL;
static pthread_mutex_t		other_select_context_lock =
	PTHREAD_MUTEX_INITIALIZER;

/*
 * Locate and load the appropriate plugin
 */
static slurm_select_ops_t *_other_select_get_ops(slurm_select_context_t *c)
{
	/*
	 * Must be synchronized with slurm_select_ops_t in node_select.h and
	 * the list in node_select.c:_select_get_ops().
	 */
	static const char *syms[] = {
		"plugin_id",
		"select_p_state_save",
		"select_p_state_restore",
		"select_p_job_init",
		"select_p_node_init",
		"select_p_block_init",
		"select_p_job_test",
		"select_p_job_begin",
		"select_p_job_ready",
		"select_p_job_resized",
		"select_p_job_fini",
		"select_p_job_suspend",
		"select_p_job_resume",
		"select_p_pack_select_info",
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
		"select_p_update_block",
		"select_p_update_sub_node",
		"select_p_get_info_from_plugin",
		"select_p_update_node_config",
		"select_p_update_node_state",
		"select_p_alter_node_cnt",
		"select_p_reconfigure",
	};
	int n_syms = sizeof(syms) / sizeof(char *);

	/* Find the correct plugin. */
	c->cur_plugin = plugin_load_and_link(c->select_type, n_syms, syms,
					     (void **) &c->ops);
	if (c->cur_plugin != PLUGIN_INVALID_HANDLE)
		return &c->ops;

	if (errno != EPLUGIN_NOTFOUND) {
		error("Couldn't load specified plugin name for %s: %s",
		      c->select_type, plugin_strerror(errno));
		return NULL;
	}

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->select_type);

	/* Get plugin list. */
	if (!c->plugin_list) {
		char *plugin_dir;
		c->plugin_list = plugrack_create();
		if (!c->plugin_list) {
			error("cannot create plugin manager");
			return NULL;
		}
		plugrack_set_major_type(c->plugin_list, "select");
		plugrack_set_paranoia(c->plugin_list,
				      PLUGRACK_PARANOIA_NONE,
				      0);
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir(c->plugin_list, plugin_dir);
		xfree(plugin_dir);
	}

	c->cur_plugin = plugrack_use_by_type(c->plugin_list, c->select_type);
	if (c->cur_plugin == PLUGIN_INVALID_HANDLE) {
		error("cannot find node selection plugin for %s",
		      c->select_type);

		return NULL;
	}

	/* Dereference the API. */
	if (plugin_get_syms(c->cur_plugin,
			    n_syms,
			    syms,
			    (void **) &c->ops) < n_syms) {
		error("incomplete node selection plugin detected");
		return NULL;
	}

	return &c->ops;
}

/*
 * Destroy a node selection context
 */
static int _other_select_context_destroy(slurm_select_context_t *c)
{
	int rc = SLURM_SUCCESS;
	/*
	 * Must check return code here because plugins might still
	 * be loaded and active.
	 */
	if (c->plugin_list) {
		if (plugrack_destroy(c->plugin_list) != SLURM_SUCCESS)
			rc = SLURM_ERROR;
	} else {
		plugin_unload(c->cur_plugin);
	}

	xfree(c->select_type);

	return rc;
}

/*
 * Initialize context for node selection plugin
 */
extern int other_select_init(void)
{
	int retval = SLURM_SUCCESS;
	char *select_type = NULL;

	slurm_mutex_lock(&other_select_context_lock);

	if (other_select_context)
		goto done;

	/*
	 * FIXME: At the moment the smallest Cray allocation unit are still
	 * full nodes. Node sharing (even across NUMA sockets of the same
	 * node) is, as of CLE 3.1 (summer 2010) still not supported, i.e.
	 * as per the LIMITATIONS section of the aprun(1) manpage of the
	 * 3.1.27A release).
	 * Hence for the moment we can only use select/linear.  If some
	 * time in the future this is allowable use code such as this
	 * to make things switch to the cons_res plugin.
	 * if (slurmctld_conf.select_type_param & CR_CONS_RES)
	 *	select_type = "select/cons_res";
	 * else
	 *	select_type = "select/linear";
	 */
	select_type = "select/linear";

	other_select_context = xmalloc(sizeof(slurm_select_context_t));
	other_select_context->select_type = xstrdup(select_type);
	other_select_context->cur_plugin = PLUGIN_INVALID_HANDLE;
	other_select_context->select_errno = SLURM_SUCCESS;

	if (!_other_select_get_ops(other_select_context)) {
		error("cannot resolve acct_storage plugin operations");
		_other_select_context_destroy(other_select_context);
		other_select_context = NULL;
		retval = SLURM_ERROR;
	}

done:
	slurm_mutex_unlock(&other_select_context_lock);
	return retval;
}

extern int other_select_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&other_select_context_lock);
	if (!other_select_context)
		goto fini;

	rc = _other_select_context_destroy(other_select_context);
	other_select_context = NULL;
fini:
	slurm_mutex_unlock(&other_select_context_lock);
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

	return (*(other_select_context->ops.state_save))(dir_name);
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

	return (*(other_select_context->ops.state_restore))(dir_name);
}

/*
 * Note the initialization of job records, issued upon restart of
 * slurmctld and used to synchronize any job state.
 */
extern int other_job_init(List job_list)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.job_init))(job_list);
}

/*
 * Note re/initialization of node record data structure
 * IN node_ptr - current node data
 * IN node_count - number of node entries
 */
extern int other_node_init(struct node_record *node_ptr, int node_cnt)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.node_init))(node_ptr, node_cnt);
}


/*
 * Note re/initialization of block record data structure
 * IN block_list - list of partition records
 */
extern int other_block_init(List block_list)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.block_init))(block_list);
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
 * RET zero on success, EINVAL otherwise
 */
extern int other_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.job_test))
		(job_ptr, bitmap,
		 min_nodes, max_nodes,
		 req_nodes, mode,
		 preemptee_candidates,
		 preemptee_job_list);
}

/*
 * Note initiation of job is about to begin. Called immediately
 * after other_job_test(). Executed from slurmctld.
 * IN job_ptr - pointer to job being initiated
 */
extern int other_job_begin(struct job_record *job_ptr)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.job_begin))
		(job_ptr);
}

/*
 * determine if job is ready to execute per the node select plugin
 * IN job_ptr - pointer to job being tested
 * RET: -2 fatal error, -1 try again, 1 if ready to execute,
 *	0 not ready to execute
 */
extern int other_job_ready(struct job_record *job_ptr)
{
	if (other_select_init() < 0)
		return -1;

	return (*(other_select_context->ops.job_ready))
		(job_ptr);
}

/*
 * Modify internal data structures for a job that has changed size
 *	Only support jobs shrinking now.
 * RET: 0 or an error code
 */
extern int other_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	if (other_select_init() < 0)
		return -1;

	return (*(other_select_context->ops.job_resized))
		(job_ptr, node_ptr);
}

/*
 * Note termination of job is starting. Executed from slurmctld.
 * IN job_ptr - pointer to job being terminated
 */
extern int other_job_fini(struct job_record *job_ptr)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.job_fini))
		(job_ptr);
}

/*
 * Suspend a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being suspended
 * RET SLURM_SUCCESS or error code
 */
extern int other_job_suspend(struct job_record *job_ptr)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.job_suspend))
		(job_ptr);
}

/*
 * Resume a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being resumed
 * RET SLURM_SUCCESS or error code
 */
extern int other_job_resume(struct job_record *job_ptr)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.job_resume))
		(job_ptr);
}

extern int other_pack_select_info(time_t last_query_time, uint16_t show_flags,
				  Buf *buffer, uint16_t protocol_version)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.pack_select_info))
		(last_query_time, show_flags, buffer, protocol_version);
}

extern int other_select_nodeinfo_pack(select_nodeinfo_t *nodeinfo,
					 Buf buffer,
					 uint16_t protocol_version)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.nodeinfo_pack))
		(nodeinfo, buffer, protocol_version);
}

extern int other_select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo,
					Buf buffer,
					uint16_t protocol_version)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.nodeinfo_unpack))
		(nodeinfo, buffer, protocol_version);
}

extern select_nodeinfo_t *other_select_nodeinfo_alloc(uint32_t size)
{
	if (other_select_init() < 0)
		return NULL;

	return (*(other_select_context->ops.nodeinfo_alloc))(size);
}

extern int other_select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.nodeinfo_free))(nodeinfo);
}

extern int other_select_nodeinfo_set_all(time_t last_query_time)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.nodeinfo_set_all))(last_query_time);
}

extern int other_select_nodeinfo_set(struct job_record *job_ptr)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.nodeinfo_set))(job_ptr);
}

extern int other_select_nodeinfo_get(select_nodeinfo_t *nodeinfo,
					enum select_nodedata_type dinfo,
					enum node_states state,
					void *data)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.nodeinfo_get))
		(nodeinfo, dinfo, state, data);
}

extern select_jobinfo_t *other_select_jobinfo_alloc(void)
{
	if (other_select_init() < 0)
		return NULL;

	return (*(other_select_context->ops.jobinfo_alloc))();;
}

/* free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 */
extern int other_select_jobinfo_free(select_jobinfo_t *jobinfo)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;
	return (*(other_select_context->ops.jobinfo_free))(jobinfo);
}

extern int other_select_jobinfo_set(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.jobinfo_set))
		(jobinfo, data_type, data);
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

	return (*(other_select_context->ops.jobinfo_get))
		(jobinfo, data_type, data);
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

	return (*(other_select_context->ops.jobinfo_copy))(jobinfo);
}

/* pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * RET         - slurm error code
 */
extern int other_select_jobinfo_pack(select_jobinfo_t *jobinfo,
				     Buf buffer,
				     uint16_t protocol_version)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.jobinfo_pack))
		(jobinfo, buffer, protocol_version);
}

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * RET         - slurm error code
 * NOTE: returned value must be freed using other_free_jobinfo
 */
extern int other_select_jobinfo_unpack(select_jobinfo_t **jobinfo,
				       Buf buffer,
				       uint16_t protocol_version)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.jobinfo_unpack))
		(jobinfo, buffer, protocol_version);
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

	return (*(other_select_context->ops.jobinfo_sprint))
		(jobinfo, buf, size, mode);
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

	return (*(other_select_context->ops.
		  jobinfo_xstrdup))(jobinfo, mode);
}

/*
 * Update specific block (usually something has gone wrong)
 * IN block_desc_ptr - information about the block
 */
extern int other_update_block (update_block_msg_t *block_desc_ptr)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.
		  update_block))(block_desc_ptr);
}

/*
 * Update specific sub nodes (usually something has gone wrong)
 * IN block_desc_ptr - information about the block
 */
extern int other_update_sub_node (update_block_msg_t *block_desc_ptr)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.
		  update_sub_node))(block_desc_ptr);
}

/*
 * Get select data from a plugin
 * IN dinfo     - type of data to get from the node record
 *                (see enum select_plugindata_info)
 * IN/OUT data  - the data to get from node record
 */
extern int other_get_info_from_plugin (enum select_plugindata_info dinfo,
					  struct job_record *job_ptr,
					  void *data)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.
		  get_info_from_plugin))(dinfo, job_ptr, data);
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

	return (*(other_select_context->ops.
		  update_node_config))(index);
}

/*
 * Updated a node state in the plugin, this should happen when a node is
 * drained or put into a down state then changed back.
 * IN index  - index into the node record list
 * IN state  - state to update to
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int other_update_node_state (int index, uint16_t state)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.update_node_state))
		(index, state);
}

/*
 * Alter the node count for a job given the type of system we are on
 * IN/OUT job_desc  - current job desc
 */
extern int other_alter_node_cnt (enum select_node_cnt type, void *data)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.alter_node_cnt))(type, data);
}

/*
 * Note reconfiguration or change in partition configuration
 */
extern int other_reconfigure (void)
{
	if (other_select_init() < 0)
		return SLURM_ERROR;

	return (*(other_select_context->ops.reconfigure))();
}
