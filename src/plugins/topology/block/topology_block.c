/*****************************************************************************\
 *  topology_block.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Written by Dominik Bartkiewicz <bart@schedmd.com>
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

#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>

#include "src/common/slurm_xlator.h"

#include "slurm/slurm_errno.h"
#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/node_conf.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

#include "../common/common_topo.h"

#include "eval_nodes_block.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern node_record_t **node_record_table_ptr __attribute__((weak_import));
extern int node_record_count __attribute__((weak_import));
extern int active_node_record_count __attribute__((weak_import));
#else
node_record_t **node_record_table_ptr;
int node_record_count;
int active_node_record_count;
#endif

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "topology block plugin";
const char plugin_type[]        = "topology/block";
const uint32_t plugin_id = TOPOLOGY_PLUGIN_BLOCK;
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

typedef struct topoinfo_bblock {
	uint16_t block_index;
	char *name;
	char *nodes;
} topoinfo_bblock_t;

typedef struct topoinfo_block {
	uint32_t record_count; /* number of records */
	topoinfo_bblock_t *topo_array;/* the block topology records */
} topoinfo_block_t;

static void _print_topo_record(topoinfo_bblock_t * topo_ptr, char **out)
{
	char *env, *line = NULL, *pos = NULL;

	/****** Line 1 ******/
	xstrfmtcatat(line, &pos, "BlockName=%s BlockIndex=%u", topo_ptr->name,
		     topo_ptr->block_index);

	if (topo_ptr->nodes)
		xstrfmtcatat(line, &pos, " Nodes=%s", topo_ptr->nodes);

	if ((env = getenv("SLURM_TOPO_LEN")))
		xstrfmtcat(*out, "%.*s\n", atoi(env), line);
	else
		xstrfmtcat(*out, "%s\n", line);

	xfree(line);

}

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init(void)
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini(void)
{
	block_record_table_destroy();
	FREE_NULL_BITMAP(blocks_nodes_bitmap);
	return SLURM_SUCCESS;
}

/*
 * topo_build_config - build or rebuild system topology information
 *	after a system startup or reconfiguration.
 */
extern int topology_p_build_config(void)
{
	if (node_record_count)
		block_record_validate();
	return SLURM_SUCCESS;
}

extern int topology_p_eval_nodes(topology_eval_t *topo_eval)
{
	topo_eval->eval_nodes = eval_nodes_block;
	topo_eval->trump_others = true;

	return common_topo_choose_nodes(topo_eval);
}

/*
 * When TopologyParam=SwitchAsNodeRank is set, this plugin assigns a unique
 * node_rank for all nodes belonging to the same bblock.
 */
extern bool topology_p_generate_node_ranking(void)
{
	return true;
}

/*
 * topo_get_node_addr - build node address and the associated pattern
 *      based on the topology information
 *
 * example of output :
 *      address : b8.tux1
 *      pattern : block.node
 */
extern int topology_p_get_node_addr(char *node_name, char **paddr,
				    char **ppattern)
{
	return SLURM_SUCCESS;
}

extern int topology_p_split_hostlist(hostlist_t *hl, hostlist_t ***sp_hl,
				     int *count, uint16_t tree_width)
{
	return common_topo_split_hostlist_treewidth(
		hl, sp_hl, count, tree_width);
}

extern int topology_p_topology_free(void *topoinfo_ptr)
{
	int i = 0;
	topoinfo_block_t *topoinfo = topoinfo_ptr;
	if (topoinfo) {
		if (topoinfo->topo_array) {
			for (i = 0; i < topoinfo->record_count; i++) {
				xfree(topoinfo->topo_array[i].name);
				xfree(topoinfo->topo_array[i].nodes);
			}
			xfree(topoinfo->topo_array);
		}
		xfree(topoinfo);
	}
	return SLURM_SUCCESS;
}

extern int topology_p_get(topology_data_t type, void *data)
{
	int rc = SLURM_SUCCESS;

	switch (type) {
	case TOPO_DATA_TOPOLOGY_PTR:
	{
		dynamic_plugin_data_t **topoinfo_pptr = data;
		topoinfo_block_t *topoinfo_ptr =
			xmalloc(sizeof(topoinfo_block_t));

		*topoinfo_pptr = xmalloc(sizeof(dynamic_plugin_data_t));
		(*topoinfo_pptr)->data = topoinfo_ptr;
		(*topoinfo_pptr)->plugin_id = plugin_id;

		topoinfo_ptr->record_count = block_record_cnt;
		topoinfo_ptr->topo_array = xcalloc(topoinfo_ptr->record_count,
						   sizeof(topoinfo_bblock_t));

		for (int i = 0; i < topoinfo_ptr->record_count; i++) {
			topoinfo_ptr->topo_array[i].block_index =
				block_record_table[i].block_index;
			topoinfo_ptr->topo_array[i].name =
				xstrdup(block_record_table[i].name);
			topoinfo_ptr->topo_array[i].nodes =
				xstrdup(block_record_table[i].nodes);
		}
		break;
	}
	case TOPO_DATA_REC_CNT:
	{
		int *rec_cnt = data;
		*rec_cnt = block_record_cnt;
		break;
	}
	default:
		error("Unsupported option %d", type);
		rc = SLURM_ERROR;
		break;
	}

	return rc;
}

extern int topology_p_topology_pack(void *topoinfo_ptr, buf_t *buffer,
				    uint16_t protocol_version)
{
	int i;
	topoinfo_block_t *topoinfo = topoinfo_ptr;

	pack32(topoinfo->record_count, buffer);
	for (i = 0; i < topoinfo->record_count; i++) {
		pack16(topoinfo->topo_array[i].block_index, buffer);
		packstr(topoinfo->topo_array[i].name, buffer);
		packstr(topoinfo->topo_array[i].nodes, buffer);
	}
	return SLURM_SUCCESS;
}

extern int topology_p_topology_print(void *topoinfo_ptr, char *nodes_list,
				     char **out)
{
	int i, match, match_cnt = 0;;
	topoinfo_block_t *topoinfo = topoinfo_ptr;

	*out = NULL;

	if ((nodes_list == NULL) || (nodes_list[0] == '\0')) {
		if (topoinfo->record_count == 0) {
			error("No topology information available");
			return SLURM_SUCCESS;
		}

		for (i = 0; i < topoinfo->record_count; i++)
			_print_topo_record(&topoinfo->topo_array[i], out);

		return SLURM_SUCCESS;
	}

	/* Search for matching block name */
	for (i = 0; i < topoinfo->record_count; i++) {
		if (xstrcmp(topoinfo->topo_array[i].name, nodes_list))
			continue;
		_print_topo_record(&topoinfo->topo_array[i], out);
		return SLURM_SUCCESS;
	}

	/* Search for matching node name */
	for (i = 0; i < topoinfo->record_count; i++) {
		hostset_t *hs;

		if ((topoinfo->topo_array[i].nodes == NULL) ||
		    (topoinfo->topo_array[i].nodes[0] == '\0'))
			continue;
		hs = hostset_create(topoinfo->topo_array[i].nodes);
		if (hs == NULL)
			fatal("hostset_create: memory allocation failure");
		match = hostset_within(hs, nodes_list);
		hostset_destroy(hs);
		if (!match)
			continue;
		match_cnt++;
		_print_topo_record(&topoinfo->topo_array[i], out);
	}

	if (match_cnt == 0) {
		error("Topology information contains no block or "
		      "node named %s", nodes_list);
	}
	return SLURM_SUCCESS;
}

extern int topology_p_topology_unpack(void **topoinfo_pptr, buf_t *buffer,
				      uint16_t protocol_version)
{
	int i = 0;
	topoinfo_block_t *topoinfo_ptr =
		xmalloc(sizeof(topoinfo_block_t));

	*topoinfo_pptr = topoinfo_ptr;
	safe_unpack32(&topoinfo_ptr->record_count, buffer);
	safe_xcalloc(topoinfo_ptr->topo_array, topoinfo_ptr->record_count,
		     sizeof(topoinfo_bblock_t));
	for (i = 0; i < topoinfo_ptr->record_count; i++) {
		safe_unpack16(&topoinfo_ptr->topo_array[i].block_index, buffer);
		safe_unpackstr(&topoinfo_ptr->topo_array[i].name, buffer);
		safe_unpackstr(&topoinfo_ptr->topo_array[i].nodes, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	topology_p_topology_free(topoinfo_ptr);
	*topoinfo_pptr = NULL;
	return SLURM_ERROR;
}
