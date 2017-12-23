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
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "config.h"

#include <dirent.h>
#include <pthread.h>

#include "src/common/list.h"
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_selecttype_info.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

/*
 * Must be synchronized with slurm_select_ops_t in node_select.h.
 * Also must be synchronized with the other_select.c in
 * the select/other lib. (We tried to make it so we only had to
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
	"select_p_job_mem_confirm",
	"select_p_job_fini",
	"select_p_job_suspend",
	"select_p_job_resume",
	"select_p_step_pick_nodes",
	"select_p_step_start",
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
	"select_p_ba_cnodelist2bitmap",
};

strong_alias(destroy_select_ba_request,	slurm_destroy_select_ba_request);

static int select_context_cnt = -1;
static int select_context_default = -1;

static slurm_select_ops_t *ops = NULL;
static plugin_context_t **select_context = NULL;
static pthread_mutex_t select_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

typedef struct _plugin_args {
	char *plugin_type;
	char *default_plugin;
} _plugin_args_t;

static int _load_plugins(void *x, void *arg)
{
	char *plugin_name     = (char *)x;
	_plugin_args_t *pargs = (_plugin_args_t *)arg;

	select_context[select_context_cnt] =
		plugin_context_create(pargs->plugin_type, plugin_name,
				      (void **)&ops[select_context_cnt],
				      node_select_syms,
				      sizeof(node_select_syms));

	if (select_context[select_context_cnt]) {
		/* set the default */
		if (!xstrcmp(plugin_name, pargs->default_plugin))
			select_context_default = select_context_cnt;
		select_context_cnt++;
	}

	return 0;
}

/**
 * delete a block request
 */
extern void destroy_select_ba_request(void *arg)
{
	select_ba_request_t *ba_request = (select_ba_request_t *)arg;

	if (ba_request) {
		xfree(ba_request->save_name);
		FREE_NULL_LIST(ba_request->elongate_geos);

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
	char *select_type = NULL;
	int i, j, plugin_cnt;
	char *plugin_type = "select";
	List plugin_names = NULL;
	_plugin_args_t plugin_args = {0};

	if ( init_run && select_context )
		return retval;

	slurm_mutex_lock( &select_context_lock );

	if ( select_context )
		goto done;

	select_type = slurm_get_select_type();
	if (working_cluster_rec) {
		/* just ignore warnings here */
	} else {
#ifdef HAVE_BG
		if (xstrcasecmp(select_type, "select/bluegene")) {
			error("%s is incompatible with BlueGene", select_type);
			fatal("Use SelectType=select/bluegene");
		}
#else
		if (!xstrcasecmp(select_type, "select/bluegene")) {
			fatal("Requested SelectType=select/bluegene "
			      "in slurm.conf, but not running on a BG[L|P|Q] "
			      "system.  If looking to emulate a BG[L|P|Q] "
			      "system use --enable-bgl-emulation or "
			      "--enable-bgp-emulation respectively.");
		}
#endif

#ifdef HAVE_ALPS_CRAY
		if (xstrcasecmp(select_type, "select/alps")) {
			error("%s is incompatible with Cray system "
			      "running alps", select_type);
			fatal("Use SelectType=select/alps");
		}
#else
		if (!xstrcasecmp(select_type, "select/alps")) {
			fatal("Requested SelectType=select/alps "
			      "in slurm.conf, but not running on a ALPS Cray "
			      "system.  If looking to emulate a Alps Cray "
			      "system use --enable-alps-cray-emulation.");
		}
#endif

#ifdef HAVE_NATIVE_CRAY
		if (xstrcasecmp(select_type, "select/cray")) {
			error("%s is incompatible with a native Cray system.",
			      select_type);
			fatal("Use SelectType=select/cray");
		}
#else
		/* if (!xstrcasecmp(select_type, "select/cray")) { */
		/* 	fatal("Requested SelectType=select/cray " */
		/* 	      "in slurm.conf, but not running on a native Cray " */
		/* 	      "system.  If looking to run on a Cray " */
		/* 	      "system natively use --enable-native-cray."); */
		/* } */
#endif
	}

	select_context_cnt = 0;

	plugin_args.plugin_type    = plugin_type;
	plugin_args.default_plugin = select_type;

	if (only_default) {
		plugin_names = list_create(slurm_destroy_char);
		list_append(plugin_names, xstrdup(select_type));
	} else {
		plugin_names = plugin_get_plugins_of_type(plugin_type);
	}
	if (plugin_names && (plugin_cnt = list_count(plugin_names))) {
		ops = xmalloc(sizeof(slurm_select_ops_t) * plugin_cnt);
		select_context = xmalloc(sizeof(plugin_context_t *) *
					 plugin_cnt);
		list_for_each(plugin_names, _load_plugins, &plugin_args);
	}


	if (select_context_default == -1)
		fatal("Can't find plugin for %s", select_type);

	/* Ensure that plugin_id is valid and unique */
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
	if (!working_cluster_rec) {
		if (select_running_linear_based()) {
			uint16_t cr_type = slurm_get_select_type_param();
			if (cr_type & (CR_CPU | CR_CORE | CR_SOCKET)) {
				fatal("Invalid SelectTypeParameters for "
				      "%s: %s (%u), it can't contain "
				      "CR_(CPU|CORE|SOCKET).",
				      select_type,
				      select_type_param_string(cr_type),
				      cr_type);
			}
		}
	}

	xfree(select_type);
	FREE_NULL_LIST(plugin_names);

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
	static bool cray_other_cons_res = false;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;
again:
	for (i=0; i<select_context_cnt; i++) {
		if (*(ops[i].plugin_id) == plugin_id)
			break;
	}
	if (i >= select_context_cnt) {
		/*
		 * Put on the extra cray plugin that doesn't get generated
		 * automatically.
		 */
		if (!cray_other_cons_res &&
		    ((plugin_id == SELECT_PLUGIN_CRAY_CONS_RES) ||
		     (plugin_id == SELECT_PLUGIN_CRAY_LINEAR))) {
			char *type = "select", *name = "select/cray";
			uint16_t save_params = slurm_get_select_type_param();
			uint16_t params;
			int cray_plugin_id;

			cray_other_cons_res = true;

			if (plugin_id == SELECT_PLUGIN_CRAY_LINEAR) {
				params = save_params & ~CR_OTHER_CONS_RES;
				cray_plugin_id = SELECT_PLUGIN_CRAY_CONS_RES;
			} else {
				params = save_params | CR_OTHER_CONS_RES;
				cray_plugin_id = SELECT_PLUGIN_CRAY_LINEAR;
			}

			for (i=0; i<select_context_cnt; i++) {
				if (*(ops[i].plugin_id) == cray_plugin_id)
					break;
			}

			if (i >= select_context_cnt)
				goto end_it;

			slurm_mutex_lock(&select_context_lock);
			slurm_set_select_type_param(params);
			plugin_context_destroy(select_context[i]);
			select_context[i] =
				plugin_context_create(type, name,
						      (void **)&ops[i],
						      node_select_syms,
						      sizeof(node_select_syms));
			slurm_set_select_type_param(save_params);
			slurm_mutex_unlock(&select_context_lock);
			goto again;
		}

	end_it:
		return SLURM_ERROR;
	}
	return i;
}

/* Get the plugin ID number. Unique for each select plugin type */
extern int select_get_plugin_id(void)
{
	int plugin_pos;

	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	plugin_pos = working_cluster_rec ?
		working_cluster_rec->plugin_id_select : select_context_default;

	return *(ops[plugin_pos].plugin_id);
}

/* If the slurmctld is running a linear based select plugin return 1
 * else 0. */
extern int select_running_linear_based(void)
{
	int rc = 0;

	if (slurm_select_init(0) < 0)
		return 0;

	switch (*(ops[select_context_default].plugin_id)) {
	case 102: // select/linear
	case 104: // select/alps -> linear
	case 107: // select/cray -> linear
		rc = 1;
		break;
	default:
		rc = 0;
		break;
	}
	return rc;
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
 * Pass job-step signal to plugin before signaling any job steps, so that
 * any signal-dependent actions can be taken.
 * IN job_ptr - job to be signaled
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
 * Confirm that a job's memory allocation is still valid after a node is
 * restarted. This is an issue if the job is allocated all of the memory on a
 * node and that node is restarted with a different memory size than at the time
 * it is allocated to the job. This would mostly be an issue on an Intel KNL
 * node where the memory size would vary with the MCDRAM cache mode.
 */
extern int select_g_job_mem_confirm(struct job_record *job_ptr)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].job_mem_confirm)) (job_ptr);
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
 * OUT avail_nodes - bitmap of available nodes according to the plugin
 *                  (not always set).
 * RET map of slurm nodes to be used for step, NULL on failure
 */
extern bitstr_t *select_g_step_pick_nodes(struct job_record *job_ptr,
					  dynamic_plugin_data_t *step_jobinfo,
					  uint32_t node_count,
					  bitstr_t **avail_nodes)
{
	if (slurm_select_init(0) < 0)
		return NULL;

	xassert(step_jobinfo);

	return (*(ops[select_context_default].step_pick_nodes))
		(job_ptr, step_jobinfo->data, node_count, avail_nodes);
}

/*
 * Post pick_nodes operations for the step.
 * IN/OUT step_ptr - step pointer to operate on.
 */
extern int select_g_step_start(struct step_record *step_ptr)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].step_start))
		(step_ptr);
}

/*
 * clear what happened in select_g_step_pick_nodes
 * IN/OUT step_ptr - Flush the resources from the job and step.
 * IN killing_step - if true then we are just starting to kill the step
 *                   if false, the step is completely terminated
 */
extern int select_g_step_finish(struct step_record *step_ptr, bool killing_step)
{
	if (slurm_select_init(0) < 0)
		return SLURM_ERROR;

	return (*(ops[select_context_default].step_finish))
		(step_ptr, killing_step);
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

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

	if (nodeinfo) {
		if (nodeinfo->data)
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

	if (nodeinfo) {
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

	if (jobinfo) {
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

	if (jobinfo) {
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
	if (jobinfo) {
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

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
	if (jobinfo) {
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

	if (jobinfo) {
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
 * IN/OUT resv_desc_ptr - reservation request - select_jobinfo can be
 *	updated in the plugin
 * IN node_cnt - count of required nodes
 * IN/OUT avail_bitmap - nodes available for the reservation
 * IN/OUT core_bitmap - cores which can not be used for this
 *	reservation IN, and cores to be used in the reservation OUT
 *	(flush bitstr then apply only used cores)
 * RET - nodes selected for use by the reservation
 */
extern bitstr_t * select_g_resv_test(resv_desc_msg_t *resv_desc_ptr,
				     uint32_t node_cnt,
				     bitstr_t *avail_bitmap,
				     bitstr_t **core_bitmap)
{
	if (slurm_select_init(0) < 0)
		return NULL;

	return (*(ops[select_context_default].resv_test))
		(resv_desc_ptr, node_cnt, avail_bitmap, core_bitmap);
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

extern bitstr_t *select_g_ba_cnodelist2bitmap(char *cnodelist)
{
	uint32_t plugin_id;

	if (slurm_select_init(0) < 0)
		return NULL;

	if (working_cluster_rec)
		plugin_id = working_cluster_rec->plugin_id_select;
	else
		plugin_id = select_context_default;

	return (*(ops[plugin_id].ba_cnodelist2bitmap))(cnodelist);
}
