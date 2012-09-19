/*****************************************************************************\
 *  node_select.c - node selection plugin wrapper.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <pthread.h>
#include <dirent.h>

#include "src/common/list.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"
#include "src/common/node_select.h"

/*
 * Must be synchronized with slurm_select_ops_t in node_select.h.
 * Also must be synchronized with the other_plugin.c in
 * the select/cray plugin. (We tried to make it so we only had to
 * define it once, but it didn't seem to work.)
 */
const char *node_select_syms[] = {
	"plugin_id",
	"select_p_state_save",
	"select_p_state_restore",
	"select_p_job_init",
	"select_p_node_ranking",
	"select_p_node_init",
	"select_p_block_init",
	"select_p_job_test",
	"select_p_job_begin",
	"select_p_job_ready",
	"select_p_job_expand_allow",
	"select_p_job_expand",
	"select_p_job_resized",
	"select_p_job_signal",
	"select_p_job_fini",
	"select_p_job_suspend",
	"select_p_job_resume",
	"select_p_step_pick_nodes",
	"select_p_step_finish",
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
	"select_p_fail_cnode",
	"select_p_get_info_from_plugin",
	"select_p_update_node_config",
	"select_p_update_node_state",
	"select_p_alter_node_cnt",
	"select_p_reconfigure",
	"select_p_resv_test",
	"select_p_ba_init",
	"select_p_ba_fini",
	"select_p_ba_get_dims",
};

strong_alias(destroy_select_ba_request,	slurm_destroy_select_ba_request);

static int select_context_cnt = -1;
static int select_context_default = -1;

static slurm_select_ops_t *ops = NULL;
static plugin_context_t **select_context = NULL;
static pthread_mutex_t select_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;
/**
 * delete a block request
 */
extern void destroy_select_ba_request(void *arg)
{
	select_ba_request_t *ba_request = (select_ba_request_t *)arg;

	if (ba_request) {
		xfree(ba_request->save_name);
		if (ba_request->elongate_geos)
			list_destroy(ba_request->elongate_geos);

		xfree(ba_request->blrtsimage);
		xfree(ba_request->linuximage);
		xfree(ba_request->mloaderimage);
		xfree(ba_request->ramdiskimage);

		xfree(ba_request);
	}
}

/**
 * print a block request
 */
extern void print_select_ba_request(select_ba_request_t* ba_request)
{
	int dim;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	uint16_t cluster_dims = slurmdb_setup_cluster_dims();

	if (ba_request == NULL){
		error("print_ba_request Error, request is NULL");
		return;
	}
	debug("  ba_request:");
	debug("    geometry:\t");
	for (dim=0; dim<cluster_dims; dim++){
		debug("%d", ba_request->geometry[dim]);
	}
	debug("        size:\t%d", ba_request->size);
	if (cluster_flags & CLUSTER_FLAG_BGQ) {
		for (dim=0; dim<cluster_dims; dim++)
			debug("   conn_type:\t%d", ba_request->conn_type[dim]);
	} else
		debug("   conn_type:\t%d", ba_request->conn_type[0]);

	debug("      rotate:\t%d", ba_request->rotate);
	debug("    elongate:\t%d", ba_request->elongate);
}

extern int select_char2coord(char coord)
{
	if ((coord >= '0') && (coord <= '9'))
		return (coord - '0');
	if ((coord >= 'A') && (coord <= 'Z'))
		return ((coord - 'A') + 10);
	return -1;
}

/*
 * Initialize context for node selection plugin
 */
extern int slurm_select_init(bool only_default)
{
	int retval = SLURM_SUCCESS;
	char *type = NULL;
	int i, j, len;
	DIR *dirp;
	struct dirent *e;
	char *dir_array = NULL, *head = NULL;
	char *plugin_type = "select";

	if ( init_run && select_context )
		return retval;

	slurm_mutex_lock( &select_context_lock );

	if ( select_context )
		goto done;

	type = slurm_get_select_type();
	if (working_cluster_rec) {
		/* just ignore warnings here */
	} else {
#ifdef HAVE_XCPU
		if (strcasecmp(type, "select/linear")) {
			error("%s is incompatible with XCPU use", type);
			fatal("Use SelectType=select/linear");
		}
#endif
		if (!strcasecmp(type, "select/linear")) {
			uint16_t cr_type = slurm_get_select_type_param();
			if ((cr_type & CR_SOCKET) || (cr_type & CR_CORE) ||
			    (cr_type & CR_CPU))
				fatal("Invalid SelectTypeParameter "
				      "for select/linear");
		}

#ifdef HAVE_BG
		if (strcasecmp(type, "select/bluegene")) {
			error("%s is incompatible with BlueGene", type);
			fatal("Use SelectType=select/bluegene");
		}
#else
		if (!strcasecmp(type, "select/bluegene")) {
			fatal("Requested SelectType=select/bluegene "
			      "in slurm.conf, but not running on a BG[L|P|Q] "
			      "system.  If looking to emulate a BG[L|P|Q] "
			      "system use --enable-bgl-emulation or "
			      "--enable-bgp-emulation respectively.");
		}
#endif

#ifdef HAVE_CRAY
		if (strcasecmp(type, "select/cray")) {
			error("%s is incompatible with Cray", type);
			fatal("Use SelectType=select/cray");
		}
#else
		if (!strcasecmp(type, "select/cray")) {
			fatal("Requested SelectType=select/cray "
			      "in slurm.conf, but not running on a Cray "
			      "system.  If looking to emulate a Cray "
			      "system use --enable-cray-emulation.");
		}
#endif
	}

	select_context_cnt = 0;
	if (only_default) {
		ops = xmalloc(sizeof(slurm_select_ops_t));
		select_context = xmalloc(sizeof(plugin_context_t));
		if ((select_context[0] = plugin_context_create(
			     plugin_type, type, (void **)&ops[0],
			     node_select_syms, sizeof(node_select_syms)))) {
			select_context_default = 0;
			select_context_cnt++;
		}
		goto skip_load_all;
	}

	if (!(dir_array = slurm_get_plugin_dir())) {
		error("plugin_load_and_link: No plugin dir given");
		goto done;
	}

	head = dir_array;
	for (i=0; ; i++) {
		bool got_colon = 0;
		if (dir_array[i] == ':') {
			dir_array[i] = '\0';
			got_colon = 1;
		} else if(dir_array[i] != '\0')
			continue;

		/* Open the directory. */
		if(!(dirp = opendir(head))) {
			error("cannot open plugin directory %s", head);
			goto done;
		}

		while (1) {
			char full_name[128];

			if(!(e = readdir( dirp )))
				break;
			/* Check only files with select_ in them. */
			if (strncmp(e->d_name, "select_", 7))
				continue;

			len = strlen(e->d_name);
#if defined(__CYGWIN__)
			len -= 4;
#else
			len -= 3;
#endif
			/* Check only shared object files */
			if (strcmp(e->d_name+len,
#if defined(__CYGWIN__)
				   ".dll"
#else
				   ".so"
#endif
				    ))
				continue;
			/* add one for the / */
			len++;
			xassert(len<sizeof(full_name));
			snprintf(full_name, len, "select/%s", e->d_name+7);
			for (j=0; j<select_context_cnt; j++) {
				if (!strcmp(full_name,
					    select_context[j]->type))
					break;
			}
			if (j >= select_context_cnt) {
				xrealloc(ops,
					 (sizeof(slurm_select_ops_t) *
					  (select_context_cnt + 1)));
				xrealloc(select_context,
					 (sizeof(plugin_context_t) *
					  (select_context_cnt + 1)));

				select_context[select_context_cnt] =
					plugin_context_create(
						plugin_type, full_name,
						(void **)&ops[
							select_context_cnt],
						node_select_syms,
						sizeof(node_select_syms));
				if (select_context[select_context_cnt]) {
					/* set the default */
					if (!strcmp(full_name, type))
						select_context_default =
							select_context_cnt;
					select_context_cnt++;
				}
			}
		}

		closedir(dirp);

		if (got_colon) {
			head = dir_array + i + 1;
		} else
			break;
	}

skip_load_all:
	if (select_context_default == -1)
		fatal("Can't find plugin for %s", type);

	/* Insure that plugin_id is valid and unique */
	for (i=0; i<select_context_cnt; i++) {
		for (j=i+1; j<select_context_cnt; j++) {
			if (*(ops[i].plugin_id) !=
			    *(ops[j].plugin_id))
				continue;
			fatal("SelectPlugins: Duplicate plugin_id %u for "
			      "%s and %s",
			      *(ops[i].plugin_id),
			      select_context[i]->type,
			      select_context[j]->type);
		}
		if (*(ops[i].plugin_id) < 100) {
			fatal("SelectPlugins: Invalid plugin_id %u (<100) %s",
			      *(ops[i].plugin_id),
			      select_context[i]->type);
		}

	}
	init_run = true;

done:
	slurm_mutex_unlock( &select_context_lock );
	xfree(type);
	xfree(dir_array);
	return retval;
}

extern int slurm_select_fini(void)
{
	int rc = SLURM_SUCCESS, i, j;

	slurm_mutex_lock(&select_context_lock);
	if (!select_context)
		goto fini;

	init_run = false;
	for (i=0; i<select_context_cnt; i++) {
		j = plugin_context_destroy(select_context[i]);
		if (j != SLURM_SUCCESS)
			rc = j;
	}
	xfree(ops);
	xfree(select_context);
	select_context_cnt = -1;

fini:	slurm_mutex_unlock(&select_context_lock);
	return rc;
}

/* Get this plugin's sequence number in SLURM's internal tables */
extern int select_get_plugin_id_pos(uint32_t plugin_id)
{
	int i;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	for (i=0; i<select_context_cnt; i++) {
		if (*(ops[i].plugin_id) == plugin_id)
			break;
	}
	if (i >= select_context_cnt)
		return SLURM_ERROR;
	return i;
}

/* Get the plugin ID number. Unique for each select plugin type */
extern int select_get_plugin_id(void)
{
	if (slurm_select_init(0) < 0)
		return 0;

	return *(ops[select_context_default].plugin_id);
}

/*
 * Save any global state information
 * IN dir_name - directory into which the data can be stored
 */
extern int select_g_state_save(char *dir_name)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].state_save))
		(dir_name);
}

/*
 * Initialize context for node selection plugin and
 * restore any global state information
 * IN dir_name - directory from which the data can be restored
 */
extern int select_g_state_restore(char *dir_name)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].state_restore))
		(dir_name);
}

/*
 * Note the initialization of job records, issued upon restart of
 * slurmctld and used to synchronize any job state.
 */
extern int select_g_job_init(List job_list)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].job_init))
		(job_list);
}

/*
 * Assign a 'node_rank' value to each of the node_ptr entries.
 * IN node_ptr - current node data
 * IN node_count - number of node entries
 * Return true if node ranking was performed, false if not.
 */
extern bool select_g_node_ranking(struct node_record *node_ptr, int node_cnt)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].node_ranking))
		(node_ptr, node_cnt);
}

/*
 * Note re/initialization of node record data structure
 * IN node_ptr - current node data
 * IN node_count - number of node entries
 */
extern int select_g_node_init(struct node_record *node_ptr, int node_cnt)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].node_init))
		(node_ptr, node_cnt);
}


/*
 * Note re/initialization of block record data structure
 * IN block_list - list of partition records
 */
extern int select_g_block_init(List block_list)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].block_init))
		(block_list);
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
 * IN exc_core_bitmap - cores used in reservations and not usable
 * RET zero on success, EINVAL otherwise
 */
extern int select_g_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list,
			     bitstr_t *exc_core_bitmap)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].job_test))
		(job_ptr, bitmap,
		 min_nodes, max_nodes,
		 req_nodes, mode,
		 preemptee_candidates, preemptee_job_list,
		 exc_core_bitmap);
}

/*
 * Note initiation of job is about to begin. Called immediately
 * after select_g_job_test(). Executed from slurmctld.
 * IN job_ptr - pointer to job being initiated
 */
extern int select_g_job_begin(struct job_record *job_ptr)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].job_begin))
		(job_ptr);
}

/*
 * determine if job is ready to execute per the node select plugin
 * IN job_ptr - pointer to job being tested
 * RET: -2 fatal error, -1 try again, 1 if ready to execute,
 *	0 not ready to execute
 */
extern int select_g_job_ready(struct job_record *job_ptr)
{
	if (slurm_select_init(0) < 0)
		return -1;

	return (*(ops[select_context_default].job_ready))
		(job_ptr);
}

/*
 * Test if job expansion is supported
 */
extern bool select_g_job_expand_allow(void)
{
	if (slurm_select_init(0) < 0)
		return false;

	return (*(ops[select_context_default].job_expand_allow))
		();
}

/*
 * Move the resource allocated to one job into that of another job.
 *	All resources are removed from "from_job_ptr" and moved into
 *	"to_job_ptr". Also see other_job_resized().
 * RET: 0 or an error code
 */
extern int select_g_job_expand(struct job_record *from_job_ptr,
			       struct job_record *to_job_ptr)
{
	if (slurm_select_init(0) < 0)
		return -1;

	return (*(ops[select_context_default].job_expand))
		(from_job_ptr, to_job_ptr);
}

/*
 * Modify internal data structures for a job that has changed size
 *	Only support jobs shrinking now.
 * RET: 0 or an error code
 */
extern int select_g_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	if (slurm_select_init(0) < 0)
		return -1;

	return (*(ops[select_context_default].job_resized))
		(job_ptr, node_ptr);
}

/*
 * Pass job-step signal to plugin before signalling any job steps, so that
 * any signal-dependent actions can be taken.
 * IN job_ptr - job to be signalled
 * IN signal  - signal(7) number
 */
extern int select_g_job_signal(struct job_record *job_ptr, int signal)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].job_signal))
		(job_ptr, signal);
}

/*
 * Note termination of job is starting. Executed from slurmctld.
 * IN job_ptr - pointer to job being terminated
 */
extern int select_g_job_fini(struct job_record *job_ptr)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].job_fini))
		(job_ptr);
}

/*
 * Suspend a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being suspended
 * IN indf_susp - set if job is being suspended indefinitely by user
 *                or admin, otherwise suspended for gang scheduling
 * RET SLURM_SUCCESS or error code
 */
extern int select_g_job_suspend(struct job_record *job_ptr, bool indf_susp)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].job_suspend))
		(job_ptr, indf_susp);
}

/*
 * Resume a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being resumed
 * IN indf_susp - set if job is being resumed from indefinite suspend by user
 *                or admin, otherwise resume from gang scheduling
 * RET SLURM_SUCCESS or error code
 */
extern int select_g_job_resume(struct job_record *job_ptr, bool indf_susp)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].job_resume))
		(job_ptr, indf_susp);
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
 * RET map of slurm nodes to be used for step, NULL on failure
 */
extern bitstr_t *select_g_step_pick_nodes(struct job_record *job_ptr,
					  dynamic_plugin_data_t *step_jobinfo,
					  uint32_t node_count)
{
	if (slurm_select_init(0) < 0)
		return NULL;

	xassert(step_jobinfo);

	return (*(ops[select_context_default].step_pick_nodes))
		(job_ptr, step_jobinfo->data, node_count);
}

/*
 * clear what happened in select_g_step_pick_nodes
 * IN/OUT step_ptr - Flush the resources from the job and step.
 */
extern int select_g_step_finish(struct step_record *step_ptr)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].step_finish))
		(step_ptr);
}

extern int select_g_pack_select_info(time_t last_query_time,
				     uint16_t show_flags, Buf *buffer,
				     uint16_t protocol_version)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].pack_select_info))
		(last_query_time, show_flags, buffer, protocol_version);
}

extern int select_g_select_nodeinfo_pack(dynamic_plugin_data_t *nodeinfo,
					 Buf buffer,
					 uint16_t protocol_version)
{
	void *data = NULL;
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	if (nodeinfo) {
		data = nodeinfo->data;
		plugin_id = nodeinfo->plugin_id;
	} else
		plugin_id = select_context_default;

	if (protocol_version >= SLURM_2_3_PROTOCOL_VERSION) {
		pack32(*(ops[plugin_id].plugin_id),
		       buffer);
	} else {
		error("select_g_select_nodeinfo_pack: protocol_version "
		      "%hu not supported", protocol_version);
	}

	return (*(ops[plugin_id].
		  nodeinfo_pack))(data, buffer, protocol_version);
}

extern int select_g_select_nodeinfo_unpack(dynamic_plugin_data_t **nodeinfo,
					   Buf buffer,
					   uint16_t protocol_version)
{
	dynamic_plugin_data_t *nodeinfo_ptr = NULL;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	nodeinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	*nodeinfo = nodeinfo_ptr;

	if (protocol_version >= SLURM_2_3_PROTOCOL_VERSION) {
		int i;
		uint32_t plugin_id;
		safe_unpack32(&plugin_id, buffer);
		for (i=0; i<select_context_cnt; i++)
			if (*(ops[i].plugin_id) == plugin_id) {
				nodeinfo_ptr->plugin_id = i;
				break;
			}
		if (i >= select_context_cnt) {
			error("we don't have select plugin type %u",plugin_id);
			goto unpack_error;
		}
	} else {
		nodeinfo_ptr->plugin_id = select_context_default;
		error("select_g_select_nodeinfo_unpack: protocol_version"
		      " %hu not supported", protocol_version);
		goto unpack_error;
	}

	if ((*(ops[nodeinfo_ptr->plugin_id].nodeinfo_unpack))
	   ((select_nodeinfo_t **)&nodeinfo_ptr->data, buffer,
	    protocol_version) != SLURM_SUCCESS)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	select_g_select_nodeinfo_free(nodeinfo_ptr);
	*nodeinfo = NULL;
	error("select_g_select_nodeinfo_unpack: unpack error");
	return SLURM_ERROR;
}

extern dynamic_plugin_data_t *select_g_select_nodeinfo_alloc(void)
{
	dynamic_plugin_data_t *nodeinfo_ptr = NULL;
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return NULL;

	plugin_id = working_cluster_rec ?
		working_cluster_rec->plugin_id_select : select_context_default;

	nodeinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	nodeinfo_ptr->plugin_id = plugin_id;
	nodeinfo_ptr->data = (*(ops[plugin_id].
				nodeinfo_alloc))();
	return nodeinfo_ptr;
}

extern int select_g_select_nodeinfo_free(dynamic_plugin_data_t *nodeinfo)
{
	int rc = SLURM_SUCCESS;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	if(nodeinfo) {
		if(nodeinfo->data)
			rc = (*(ops[nodeinfo->plugin_id].
				nodeinfo_free))(nodeinfo->data);
		xfree(nodeinfo);
	}
	return rc;
}

extern int select_g_select_nodeinfo_set_all(void)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].nodeinfo_set_all))
		();
}

extern int select_g_select_nodeinfo_set(struct job_record *job_ptr)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].nodeinfo_set))
		(job_ptr);
}

extern int select_g_select_nodeinfo_get(dynamic_plugin_data_t *nodeinfo,
					enum select_nodedata_type dinfo,
					enum node_states state,
					void *data)
{
	void *nodedata = NULL;
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	if(nodeinfo) {
		nodedata = nodeinfo->data;
		plugin_id = nodeinfo->plugin_id;
	} else
		plugin_id = select_context_default;

	return (*(ops[plugin_id].nodeinfo_get))
		(nodedata, dinfo, state, data);
}

extern dynamic_plugin_data_t *select_g_select_jobinfo_alloc(void)
{
	dynamic_plugin_data_t *jobinfo_ptr = NULL;
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return NULL;

	plugin_id = working_cluster_rec ?
		working_cluster_rec->plugin_id_select : select_context_default;

	jobinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	jobinfo_ptr->plugin_id = plugin_id;
	jobinfo_ptr->data =  (*(ops[plugin_id].
				jobinfo_alloc))();
	return jobinfo_ptr;
}

/* free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 */
extern int select_g_select_jobinfo_free(dynamic_plugin_data_t *jobinfo)
{
	int rc = SLURM_SUCCESS;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;
	if (jobinfo) {
		if (jobinfo->data) {
			rc = (*(ops[jobinfo->plugin_id].
				jobinfo_free))(jobinfo->data);
		}
		xfree(jobinfo);
	}
	return rc;
}

extern int select_g_select_jobinfo_set(dynamic_plugin_data_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	void *jobdata = NULL;
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	if(jobinfo) {
		jobdata = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = select_context_default;

	return (*(ops[plugin_id].jobinfo_set))
		(jobdata, data_type, data);
}

/* get data from a select job credential
 * IN jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN/OUT data - the data to enter into job credential
 */
extern int select_g_select_jobinfo_get(dynamic_plugin_data_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	void *jobdata = NULL;
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	if(jobinfo) {
		jobdata = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = select_context_default;

	return (*(ops[plugin_id].jobinfo_get))
		(jobdata, data_type, data);
}

/* copy a select job credential
 * IN jobinfo - the select job credential to be copied
 * RET        - the copy or NULL on failure
 * NOTE: returned value must be freed using select_g_free_jobinfo
 */
extern dynamic_plugin_data_t *select_g_select_jobinfo_copy(
	dynamic_plugin_data_t *jobinfo)
{
	dynamic_plugin_data_t *jobinfo_ptr = NULL;
	if (slurm_select_init(0) < 0)
		return NULL;

	jobinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	if(jobinfo) {
		jobinfo_ptr->plugin_id = jobinfo->plugin_id;
		jobinfo_ptr->data = (*(ops[jobinfo->plugin_id].
				       jobinfo_copy))(jobinfo->data);
	} else
		jobinfo_ptr->plugin_id = select_context_default;

	return jobinfo_ptr;
}

/* pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * RET         - slurm error code
 */
extern int select_g_select_jobinfo_pack(dynamic_plugin_data_t *jobinfo,
					Buf buffer,
					uint16_t protocol_version)
{
	void *data = NULL;
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	if (jobinfo) {
		data = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = select_context_default;

	if (protocol_version >= SLURM_2_3_PROTOCOL_VERSION) {
		pack32(*(ops[plugin_id].plugin_id), buffer);
	} else {
		error("select_g_select_jobinfo_pack: protocol_version "
		      "%hu not supported", protocol_version);
	}

	return (*(ops[plugin_id].jobinfo_pack))(data, buffer, protocol_version);
}

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * RET         - slurm error code
 * NOTE: returned value must be freed using select_g_free_jobinfo
 */
extern int select_g_select_jobinfo_unpack(dynamic_plugin_data_t **jobinfo,
					  Buf buffer,
					  uint16_t protocol_version)
{
	dynamic_plugin_data_t *jobinfo_ptr = NULL;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	jobinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	*jobinfo = jobinfo_ptr;

	if (protocol_version >= SLURM_2_3_PROTOCOL_VERSION) {
		int i;
		uint32_t plugin_id;
		safe_unpack32(&plugin_id, buffer);
		for (i=0; i<select_context_cnt; i++)
			if (*(ops[i].plugin_id) == plugin_id) {
				jobinfo_ptr->plugin_id = i;
				break;
			}
		if (i >= select_context_cnt) {
			error("we don't have select plugin type %u", plugin_id);
			goto unpack_error;
		}
	} else {
		jobinfo_ptr->plugin_id = select_context_default;
		error("select_g_select_jobinfo_unpack: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	if ((*(ops[jobinfo_ptr->plugin_id].jobinfo_unpack))
		((select_jobinfo_t **)&jobinfo_ptr->data, buffer,
		 protocol_version) != SLURM_SUCCESS)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	select_g_select_jobinfo_free(jobinfo_ptr);
	*jobinfo = NULL;
	error("select_g_select_jobinfo_unpack: unpack error");
	return SLURM_ERROR;
}

/* write select job credential to a string
 * IN jobinfo - a select job credential
 * OUT buf    - location to write job credential contents
 * IN size    - byte size of buf
 * IN mode    - print mode, see enum select_print_mode
 * RET        - the string, same as buf
 */
extern char *select_g_select_jobinfo_sprint(dynamic_plugin_data_t *jobinfo,
					    char *buf, size_t size, int mode)
{
	void *data = NULL;
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return NULL;
	if(jobinfo) {
		data = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = select_context_default;

	return (*(ops[plugin_id].
		  jobinfo_sprint))
		(data, buf, size, mode);
}
/* write select job info to a string
 * IN jobinfo - a select job credential
 * IN mode    - print mode, see enum select_print_mode
 * RET        - char * containing string of request
 */
extern char *select_g_select_jobinfo_xstrdup(
	dynamic_plugin_data_t *jobinfo, int mode)
{
	void *data = NULL;
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return NULL;

	if(jobinfo) {
		data = jobinfo->data;
		plugin_id = jobinfo->plugin_id;
	} else
		plugin_id = select_context_default;

	return (*(ops[plugin_id].
		  jobinfo_xstrdup))(data, mode);
}

/*
 * Update specific block (usually something has gone wrong)
 * IN block_desc_ptr - information about the block
 */
extern int select_g_update_block (update_block_msg_t *block_desc_ptr)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].
		  update_block))(block_desc_ptr);
}

/*
 * Update specific sub nodes (usually something has gone wrong)
 * IN block_desc_ptr - information about the block
 */
extern int select_g_update_sub_node (update_block_msg_t *block_desc_ptr)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].
		  update_sub_node))(block_desc_ptr);
}

/*
 * Fail certain cnodes in a blocks midplane (usually comes from the
 *        IBM runjob mux)
 * IN step_ptr - step that has failed cnodes
 */
extern int select_g_fail_cnode (struct step_record *step_ptr)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].fail_cnode))(step_ptr);
}

/*
 * Get select data from a plugin
 * IN dinfo     - type of data to get from the node record
 *                (see enum select_plugindata_info)
 * IN/OUT data  - the data to get from node record
 */
extern int select_g_get_info_from_plugin (enum select_plugindata_info dinfo,
					  struct job_record *job_ptr,
					  void *data)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].
		  get_info_from_plugin))(dinfo, job_ptr, data);
}

/*
 * Updated a node configuration. This happens when a node registers with
 *     more resources than originally configured (e.g. memory).
 * IN index  - index into the node record list
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int select_g_update_node_config (int index)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].
		  update_node_config))(index);
}

/*
 * Updated a node state in the plugin, this should happen when a node is
 * drained or put into a down state then changed back.
 * IN index  - index into the node record list
 * IN state  - state to update to
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int select_g_update_node_state (struct node_record *node_ptr)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].update_node_state))
		(node_ptr);
}

/*
 * Alter the node count for a job given the type of system we are on
 * IN/OUT job_desc  - current job desc
 */
extern int select_g_alter_node_cnt (enum select_node_cnt type, void *data)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	if (type == SELECT_GET_NODE_SCALING) {
		/* default to one, so most plugins don't have to */
		uint32_t *nodes = (uint32_t *)data;
		*nodes = 1;
	}
	return (*(ops[select_context_default].alter_node_cnt))(type, data);
}

/*
 * Note reconfiguration or change in partition configuration
 */
extern int select_g_reconfigure (void)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].reconfigure))();
}

/*
 * select_g_resv_test - Identify the nodes which "best" satisfy a reservation
 *	request. "best" is defined as either single set of consecutive nodes
 *	satisfying the request and leaving the minimum number of unused nodes
 *	OR the fewest number of consecutive node sets
 * IN avail_bitmap - nodes available for the reservation
 * IN node_cnt - count of required nodes
 * IN core_cnt - count of required cores
 * IN core_bitmap - cores to be excluded for this reservation
 * RET - nodes selected for use by the reservation
 */
extern bitstr_t * select_g_resv_test(bitstr_t *avail_bitmap, uint32_t node_cnt,
				     uint32_t core_cnt, bitstr_t **core_bitmap)
{
	if (slurm_select_init(0) < 0)
		return NULL;

	return (*(ops[select_context_default].resv_test))
		(avail_bitmap, node_cnt, core_cnt, core_bitmap);
}

extern void select_g_ba_init(node_info_msg_t *node_info_ptr, bool sanity_check)
{
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return;

	if (working_cluster_rec)
		plugin_id = working_cluster_rec->plugin_id_select;
	else
		plugin_id = select_context_default;

	(*(ops[plugin_id].ba_init))(node_info_ptr, sanity_check);
}

extern void select_g_ba_fini(void)
{
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return;

	if (working_cluster_rec)
		plugin_id = working_cluster_rec->plugin_id_select;
	else
		plugin_id = select_context_default;

	(*(ops[plugin_id].ba_fini))();
}

extern int *select_g_ba_get_dims(void)
{
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return NULL;

	if (working_cluster_rec)
		plugin_id = working_cluster_rec->plugin_id_select;
	else
		plugin_id = select_context_default;

	return (*(ops[plugin_id].ba_get_dims))();
}
