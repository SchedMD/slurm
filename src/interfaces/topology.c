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

static uint32_t active_topo_id;

char *topo_conf = NULL;

typedef struct slurm_topo_ops {
	uint32_t (*plugin_id);
	int		(*build_config)		( void );
	int (*eval_nodes) (topology_eval_t *topo_eval);

	bool		(*node_ranking)		( void );
	int		(*get_node_addr)	( char* node_name,
						  char** addr,
						  char** pattern );
	int (*split_hostlist) (hostlist_t *hl,
			       hostlist_t ***sp_hl,
			       int *count,
			       uint16_t tree_width);
	int (*topoinfo_free) (void *topoinfo_ptr);
	int (*get) (topology_data_t type, void *data);
	int (*topoinfo_pack) (void *topoinfo_ptr, buf_t *buffer,
			      uint16_t protocol_version);
	int (*topoinfo_print) (void *topoinfo_ptr, char *nodes_list,
			       char **out);
	int (*topoinfo_unpack) (void **topoinfo_pptr, buf_t *buffer,
				uint16_t protocol_version);
} slurm_topo_ops_t;

/*
 * Must be synchronized with slurm_topo_ops_t above.
 */
static const char *syms[] = {
	"plugin_id",
	"topology_p_build_config",
	"topology_p_eval_nodes",
	"topology_p_generate_node_ranking",
	"topology_p_get_node_addr",
	"topology_p_split_hostlist",
	"topology_p_topology_free",
	"topology_p_get",
	"topology_p_topology_pack",
	"topology_p_topology_print",
	"topology_p_topology_unpack",
};

static slurm_topo_ops_t ops;
static plugin_context_t	*g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;

/*
 * The topology plugin can not be changed via reconfiguration
 * due to background threads, job priorities, etc. slurmctld must
 * be restarted and job priority changes may be required to change
 * the topology type.
 */
extern int topology_g_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "topo";

	slurm_mutex_lock(&g_context_lock);

	if (plugin_inited)
		goto done;

	xassert(slurm_conf.topology_plugin);

	if (!topo_conf)
		topo_conf = get_extra_conf_path("topology.conf");

	g_context = plugin_context_create(plugin_type,
					  slurm_conf.topology_plugin,
					  (void **) &ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s",
		      plugin_type, slurm_conf.topology_plugin);
		retval = SLURM_ERROR;
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}
	active_topo_id = *(ops.plugin_id);
	plugin_inited = PLUGIN_INITED;
done:
	slurm_mutex_unlock(&g_context_lock);
	return retval;
}

extern int topology_g_fini(void)
{
	int rc = SLURM_SUCCESS;

	if (g_context) {
		rc = plugin_context_destroy(g_context);
		g_context = NULL;
	}

	xfree(topo_conf);

	plugin_inited = PLUGIN_NOT_INITED;

	return rc;
}

extern int topology_get_plugin_id(void)
{
	xassert(plugin_inited);

	return *(ops.plugin_id);
}

extern int topology_g_build_config(void)
{
	int rc;
	DEF_TIMERS;

	xassert(plugin_inited);

	START_TIMER;
	rc = (*(ops.build_config))();
	END_TIMER3(__func__, 20000);

	return rc;
}

extern int topology_g_eval_nodes(topology_eval_t *topo_eval)
{
	xassert(plugin_inited);

	return (*(ops.eval_nodes))(topo_eval);
}

/*
 * This operation is only supported by those topology plugins for
 * which the node ordering between slurmd and slurmctld is invariant.
 */
extern bool topology_g_generate_node_ranking(void)
{
	xassert(plugin_inited);

	return (*(ops.node_ranking))();
}

extern int topology_g_get_node_addr(char *node_name, char **addr,
				    char **pattern)
{
	xassert(plugin_inited);

	return (*(ops.get_node_addr))(node_name,addr,pattern);
}

extern int topology_g_split_hostlist(hostlist_t *hl,
				     hostlist_t ***sp_hl,
				     int *count,
				     uint16_t tree_width)
{
	int rc;
	int j, nnodes, nnodex;
	char *buf;

	nnodes = nnodex = 0;
	xassert(g_context);

	if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
		/* nnodes has to be set here as the hl is empty after the
		 * split_hostlise call.  */
		nnodes = hostlist_count(hl);
		buf = hostlist_ranged_string_xmalloc(hl);
		info("ROUTE: split_hostlist: hl=%s tree_width %u",
		     buf, tree_width);
		xfree(buf);
	}

	if (!tree_width)
		tree_width = slurm_conf.tree_width;

	rc = (*(ops.split_hostlist))(hl, sp_hl, count, tree_width);
	if (!rc && !(*count))
		rc = SLURM_ERROR;

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

	return rc;
}

extern int topology_g_get(topology_data_t type, void *data)
{
	xassert(plugin_inited);

	return (*(ops.get))(type, data);
}

extern int topology_g_topology_pack(dynamic_plugin_data_t *topoinfo,
				    buf_t *buffer, uint16_t protocol_version)
{
	xassert(plugin_inited);

	if (topoinfo->plugin_id != active_topo_id)
		return SLURM_ERROR;

	pack32(*(ops.plugin_id), buffer);
	return (*(ops.topoinfo_pack))(topoinfo->data, buffer, protocol_version);
}

extern int topology_g_topology_print(dynamic_plugin_data_t *topoinfo,
				     char *nodes_list, char **out)
{
	xassert(plugin_inited);

	if (topoinfo->plugin_id != active_topo_id)
		return SLURM_ERROR;

	return (*(ops.topoinfo_print))(topoinfo->data, nodes_list, out);
}

extern int topology_g_topology_unpack(dynamic_plugin_data_t **topoinfo,
				      buf_t *buffer, uint16_t protocol_version)
{
	dynamic_plugin_data_t *topoinfo_ptr = NULL;

	xassert(plugin_inited);

	topoinfo_ptr = xmalloc(sizeof(dynamic_plugin_data_t));
	*topoinfo = topoinfo_ptr;

	if (protocol_version >= SLURM_23_11_PROTOCOL_VERSION) {
		uint32_t plugin_id;
		safe_unpack32(&plugin_id, buffer);
		if (plugin_id != active_topo_id) {
			error("%s: topology plugin %u not active",
			      __func__, plugin_id);
			goto unpack_error;
		} else {
			 topoinfo_ptr->plugin_id = active_topo_id;
		}
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
		goto unpack_error;
	}

	if ((*(ops.topoinfo_unpack))(&topoinfo_ptr->data, buffer,
				     protocol_version) != SLURM_SUCCESS)
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

	xassert(plugin_inited);

	if (topoinfo) {
		if (topoinfo->data)
			rc = (*(ops.topoinfo_free))(topoinfo->data);
		xfree(topoinfo);
	}
	return rc;
}
