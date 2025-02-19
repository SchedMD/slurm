/*****************************************************************************\
 *  slurm_topology.c - Topology plugin function setup.
 *****************************************************************************
 *  Copyright (C) 2009-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2014 Silicon Graphics International Corp. All rights reserved.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
#include "src/interfaces/topology.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

strong_alias(topology_g_build_config, slurm_topology_g_build_config);
strong_alias(topology_g_destroy_config, slurm_topology_g_detroy_config);

typedef struct slurm_topo_ops {
	uint32_t (*plugin_id);
	char(*plugin_type);
	bool(*supports_exclusive_topo);
	int (*build_config)(topology_ctx_t *tctx);
	int (*destroy_config)(topology_ctx_t *tctx);
	int (*eval_nodes) (topology_eval_t *topo_eval);

	int (*whole_topo)(bitstr_t *node_mask, void *tctx);
	bitstr_t *(*get_bitmap)(char *name, void *tctx);
	bool (*node_ranking)(topology_ctx_t *tctx);
	int		(*get_node_addr)	( char* node_name,
						  char** addr,
						  char** pattern );
	int (*split_hostlist)(hostlist_t *hl, hostlist_t ***sp_hl, int *count,
			      uint16_t tree_width, void *tctx);
	int (*topoinfo_free) (void *topoinfo_ptr);
	int (*get)(topology_data_t type, void *data, void *tctx);
	int (*topoinfo_pack) (void *topoinfo_ptr, buf_t *buffer,
			      uint16_t protocol_version);
	int (*topoinfo_print) (void *topoinfo_ptr, char *nodes_list,
			       char **out);
	int (*topoinfo_unpack) (void **topoinfo_pptr, buf_t *buffer,
				uint16_t protocol_version);
	uint32_t (*get_fragmentation)(bitstr_t *node_mask, void *tctx);
} slurm_topo_ops_t;

/*
 * Must be synchronized with slurm_topo_ops_t above.
 */
static const char *syms[] = {
	"plugin_id",
	"plugin_type",
	"supports_exclusive_topo",
	"topology_p_build_config",
	"topology_p_destroy_config",
	"topology_p_eval_nodes",
	"topology_p_whole_topo",
	"topology_p_get_bitmap",
	"topology_p_generate_node_ranking",
	"topology_p_get_node_addr",
	"topology_p_split_hostlist",
	"topology_p_topology_free",
	"topology_p_get",
	"topology_p_topology_pack",
	"topology_p_topology_print",
	"topology_p_topology_unpack",
	"topology_p_get_fragmentation",
};

static slurm_topo_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static int g_context_num = 0;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static topology_ctx_t *tctx = NULL;
static int tctx_num = -1;

static int _get_plugin_index(int plugin_id)
{
	xassert(ops);

	for (int i = 0; i < g_context_num; i++)
		if (plugin_id == *ops[i].plugin_id)
			return i;

	return -1;
}

static int _get_plugin_index_by_type(char *type)
{
	char *plugin_type = "topo";

	for (int i = 0; i < g_context_num; i++)
		if (!xstrcmp(plugin_type, ops[i].plugin_type))
			return i;

	xrecalloc(ops, g_context_num + 1, sizeof(slurm_topo_ops_t));
	xrecalloc(g_context, g_context_num + 1, sizeof(plugin_context_t *));

	g_context[g_context_num] =
		plugin_context_create(plugin_type, type,
				      (void **) &ops[g_context_num], syms,
				      sizeof(syms));
	if (!g_context[g_context_num]) {
		error("%s: cannot create %s context for %s",
		      __func__, plugin_type, type);
		return -1;
	}

	return g_context_num++;
}

static int _get_tctx_index_by_name(char *name)
{
	for (int i = 0; i < tctx_num; i++) {
		if (!xstrcmp(name, tctx[i].name))
			return i;
	}

	return -1;
}

/*
 * The topology plugin can not be changed via reconfiguration
 * due to background threads, job priorities, etc. slurmctld must
 * be restarted and job priority changes may be required to change
 * the topology type.
 */
extern int topology_g_init(void)
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);

	if (plugin_inited)
		goto done;

	xassert(slurm_conf.topology_plugin);

	tctx = xcalloc(1, sizeof(topology_ctx_t));
	tctx[0].name = xstrdup("default");
	tctx[0].topo_conf = get_extra_conf_path("topology.conf");

	if ((tctx[0].idx =
	     _get_plugin_index_by_type(slurm_conf.topology_plugin)) < 0) {
		retval = SLURM_ERROR;
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}

	plugin_inited = PLUGIN_INITED;
	tctx_num = 1;
done:

	slurm_mutex_unlock(&g_context_lock);
	return retval;
}

extern int topology_g_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	for (int i = 0; i < g_context_num; i++) {
		int rc2 = plugin_context_destroy(g_context[i]);
		if (rc2) {
			debug("%s: %s: %s",
			      __func__, g_context[i]->type,
			      slurm_strerror(rc2));
			rc = SLURM_ERROR;
		}
	}

	xfree(ops);
	xfree(g_context);
	g_context_num = 0;

	slurm_mutex_unlock(&g_context_lock);

	return rc;
}

extern int topology_get_plugin_id(void)
{
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	return *(ops[0].plugin_id);
}

extern int topology_g_build_config(void)
{
	int rc = SLURM_SUCCESS;
	DEF_TIMERS;

	slurm_mutex_lock(&g_context_lock);
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	START_TIMER;
	for (int i = 0; i < tctx_num; i++) {
		int rc2 = (*(ops[tctx[i].idx].build_config))(&(tctx[i]));
		if (rc2) {
			debug("%s: %s: %s",
			      __func__, g_context[i]->type,
			      slurm_strerror(rc2));
			rc = SLURM_ERROR;
		}
	}
	END_TIMER3(__func__, 20000);

	slurm_mutex_unlock(&g_context_lock);

	return rc;
}

extern int topology_g_destroy_config(void)
{
	int rc = SLURM_SUCCESS;
	DEF_TIMERS;

	slurm_mutex_lock(&g_context_lock);
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	START_TIMER;
	for (int i = 0; i < tctx_num; i++) {
		int rc2 = (*(ops[tctx[i].idx].destroy_config))(&(tctx[i]));
		if (rc2) {
			debug("%s: %s: %s",
			      __func__, g_context[i]->type,
			      slurm_strerror(rc2));
			rc = SLURM_ERROR;
		}
	}
	END_TIMER3(__func__, 20000);

	slurm_mutex_unlock(&g_context_lock);

	return rc;
}
extern int topology_g_eval_nodes(topology_eval_t *topo_eval)
{
	int idx = topo_eval->job_ptr->part_ptr->topology_idx;

	xassert(plugin_inited != PLUGIN_NOT_INITED);
	xassert((idx >= 0) && (idx < tctx_num));

	topo_eval->tctx = &(tctx[idx]);

	return (*(ops[tctx[idx].idx].eval_nodes))(topo_eval);
}

extern int topology_g_whole_topo(bitstr_t *node_mask, int idx)
{
	xassert(plugin_inited);
	xassert((idx >= 0) && (idx < tctx_num));

	return (*(ops[tctx[idx].idx].whole_topo))(node_mask,
						  tctx[idx].plugin_ctx);
}

extern bool topology_g_whole_topo_enabled(int idx)
{
	xassert(plugin_inited);
	xassert((idx >= 0) && (idx < tctx_num));

	return (*(ops[tctx[idx].idx].supports_exclusive_topo));
}

/*
 * topology_g_get_bitmap - Get bitmap of nodes in topo group
 *
 * IN name of topo group
 * RET bitmap of nodes from _record_table (do not free)
 */
extern bitstr_t *topology_g_get_bitmap(char *name)
{
	xassert(plugin_inited);

	return (*(ops[tctx[0].idx].get_bitmap))(name, tctx[0].plugin_ctx);
}

/*
 * This operation is only supported by those topology plugins for
 * which the node ordering between slurmd and slurmctld is invariant.
 */
extern bool topology_g_generate_node_ranking(void)
{
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	return (*(ops[tctx[0].idx].node_ranking))(&(tctx[0]));
}

extern int topology_g_get_node_addr(char *node_name, char **addr,
				    char **pattern)
{
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	return (*(ops[tctx[0].idx].get_node_addr))(node_name, addr, pattern);
}

extern int topology_g_split_hostlist(hostlist_t *hl,
				     hostlist_t ***sp_hl,
				     int *count,
				     uint16_t tree_width)
{
	int depth, j, nnodes, nnodex;
	char *buf;

	nnodes = nnodex = 0;
	xassert(g_context);

	if (!tree_width)
		tree_width = slurm_conf.tree_width;

	if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
		/* nnodes has to be set here as the hl is empty after the
		 * split_hostlise call.  */
		nnodes = hostlist_count(hl);
		buf = hostlist_ranged_string_xmalloc(hl);
		info("ROUTE: split_hostlist: hl=%s tree_width %u",
		     buf, tree_width);
		xfree(buf);
	}

	if (hostlist_count(hl) == 1) {
		/* No need to split a list of 1. */
		char *name = hostlist_shift(hl);
		*sp_hl = xcalloc(1, sizeof(hostlist_t *));
		(*sp_hl)[0] = hostlist_create(name);
		free(name);
		*count = depth = 1;

		goto end;
	}

	depth = (*(ops[tctx[0].idx].split_hostlist))(hl, sp_hl, count,
						     tree_width,
						     tctx[0].plugin_ctx);
	if (!depth && !(*count))
		goto end;

	if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
		/* Sanity check to make sure all nodes in msg list are in
		 * a child list */
		nnodex = 0;
		for (j = 0; j < *count; j++) {
			nnodex += hostlist_count((*sp_hl)[j]);
		}
		if (nnodex != nnodes) {	/* CLANG false positive */
			info("ROUTE: number of nodes in split lists (%d)"
			     " is not equal to number in input list (%d)",
			     nnodex, nnodes);
		}
	}

end:
	return depth;
}

extern int topology_g_get(topology_data_t type, char *name, void *data)
{
	int tctx_idx = 0;
	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (type == TOPO_DATA_TCTX_IDX) {
		int tmp_idx;
		if (!name || ((tmp_idx = _get_tctx_index_by_name(name)) < 0))
			return SLURM_ERROR;
		else {
			int *tctx_idx_ptr = data;
			*tctx_idx_ptr = tmp_idx;
			return SLURM_SUCCESS;
		}
	}

	if (type == TOPO_DATA_EXCLUSIVE_TOPO && !name) {
		int *exclusive_topo = data;
		*exclusive_topo = 0;
		for (int i = 0; i < g_context_num; i++) {
			if (*ops[i].supports_exclusive_topo) {
				*exclusive_topo = 1;
				break;
			}
		}
		return SLURM_SUCCESS;
	}

	if (name) {
		tctx_idx = _get_tctx_index_by_name(name);
		if (tctx_idx < 0) {
			error("%s: topology %s not active", __func__, name);
			tctx_idx = 0;
		}
	}

	return (*(ops[tctx[tctx_idx].idx].get))(type, data,
						tctx[tctx_idx].plugin_ctx);
}

extern int topology_g_topology_pack(dynamic_plugin_data_t *topoinfo,
				    buf_t *buffer, uint16_t protocol_version)
{
	int plugin_inx = _get_plugin_index(topoinfo->plugin_id);

	xassert(plugin_inited != PLUGIN_NOT_INITED);

	/* Always pack the plugin_id */
	pack32(topoinfo->plugin_id, buffer);
	if (!topoinfo)
		return SLURM_SUCCESS;

	if (plugin_inx < 0)
		return SLURM_ERROR;

	return (*(ops[plugin_inx].topoinfo_pack))(topoinfo->data, buffer,
						  protocol_version);
}

extern int topology_g_topology_print(dynamic_plugin_data_t *topoinfo,
				     char *nodes_list, char **out)
{
	int plugin_inx = _get_plugin_index(topoinfo->plugin_id);
	xassert(plugin_inited != PLUGIN_NOT_INITED);
	xassert(topoinfo);
	if (plugin_inx < 0)
		return SLURM_ERROR;

	return (*(ops[tctx[plugin_inx].idx].topoinfo_print))(topoinfo->data,
							     nodes_list, out);
}

extern int topology_g_topology_unpack(dynamic_plugin_data_t **topoinfo,
				      buf_t *buffer, uint16_t protocol_version)
{
	dynamic_plugin_data_t *topoinfo_ptr = NULL;
	int plugin_inx;

	xassert(plugin_inited != PLUGIN_NOT_INITED);

	topoinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	*topoinfo = topoinfo_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint32_t plugin_id;
		safe_unpack32(&plugin_id, buffer);

		plugin_inx = _get_plugin_index(plugin_id);
		if (plugin_inx < 0) {
			error("%s: topology plugin %u not active",
			      __func__, plugin_id);
			goto unpack_error;
		} else {
			topoinfo_ptr->plugin_id = plugin_id;
		}
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
		goto unpack_error;
	}

	if ((*(ops[tctx[plugin_inx].idx].topoinfo_unpack))(&topoinfo_ptr->data,
							   buffer,
							   protocol_version) !=
	    SLURM_SUCCESS)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	topology_g_topology_free(topoinfo_ptr);
	*topoinfo = NULL;
	error("%s: unpack error", __func__);
	return SLURM_ERROR;
}

extern int topology_g_topology_free(dynamic_plugin_data_t *topoinfo)
{
	int rc = SLURM_SUCCESS;
	int plugin_inx = _get_plugin_index(topoinfo->plugin_id);

	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (topoinfo) {
		if (topoinfo->data)
			rc = (*(ops[plugin_inx].topoinfo_free))(topoinfo->data);
		xfree(topoinfo);
	}
	return rc;
}

extern uint32_t topology_g_get_fragmentation(bitstr_t *node_mask)
{
	xassert(plugin_inited);

	return (*(ops[tctx[0].idx].get_fragmentation))(node_mask,
						       tctx[0].plugin_ctx);
}
