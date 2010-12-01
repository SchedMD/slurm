/*****************************************************************************\
 *  topology_node_rank.c - Re-order the nodes in a cluster based upon
 *	the node's "node_rank" field as set by some other module (probably
 *	the select plugin)
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>

#include <slurm/slurm_errno.h>
#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/slurm_topology.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

#define _DEBUG 0

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
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as this API matures.
 */
const char plugin_name[]        = "topology node_rank plugin";
const char plugin_type[]        = "topology/node_rank";
const uint32_t plugin_version   = 100;

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
	return SLURM_SUCCESS;
}

/*
 * topo_build_config - build or rebuild system topology information
 *	after a system startup or reconfiguration.
 */
extern int topo_build_config(void)
{
	static bool first_run = true;
	struct node_record *node_ptr, *node_ptr2;
	int i, j, min_inx;
	uint32_t min_val;

	/* We can only re-order the nodes once at slurmctld startup.
	 * After that time, many bitmaps are created based upon the
	 * index of each node name in the array. */
	if (!first_run)
		return SLURM_SUCCESS;
	first_run = false;

	/* Now we need to sort the node records. We only need to move a few
	 * fields since the others were all initialized to identical values.
	 * The fields needing to be copied are those set by the function
	 * _build_single_nodeline_info() in src/common/read_conf.c */
	for (i=0; i<node_record_count; i++) {
		min_val = node_record_table_ptr[i].node_rank;
		min_inx = i;
		for (j=(i+1); j<node_record_count; j++) {
			if (node_record_table_ptr[j].node_rank < min_val) {
				min_val = node_record_table_ptr[j].node_rank;
				min_inx = j;
			}
		}
		if (min_inx != i) {	/* swap records */
			char *tmp_str;
			uint16_t tmp_uint16;
			uint32_t tmp_uint32;

			node_ptr =  node_record_table_ptr + i;
			node_ptr2 = node_record_table_ptr + min_inx;

			tmp_str = node_ptr->name;
			node_ptr->name  = node_ptr2->name;
			node_ptr2->name = tmp_str;

			tmp_str = node_ptr->comm_name;
			node_ptr->comm_name  = node_ptr2->comm_name;
			node_ptr2->comm_name = tmp_str;

			tmp_uint32 = node_ptr->node_rank;
			node_ptr->node_rank  = node_ptr2->node_rank;
			node_ptr2->node_rank = tmp_uint32;

			tmp_str = node_ptr->features;
			node_ptr->features  = node_ptr2->features;
			node_ptr2->features = tmp_str;

			tmp_uint16 = node_ptr->port;
			node_ptr->port  = node_ptr2->port;
			node_ptr2->port = tmp_uint16;

			tmp_str = node_ptr->reason;
			node_ptr->reason  = node_ptr2->reason;
			node_ptr2->reason = tmp_str;

			tmp_uint32 = node_ptr->weight;
			node_ptr->weight  = node_ptr2->weight;
			node_ptr2->weight = tmp_uint32;
		}
	}

#if _DEBUG
	/* Log the results */
	for (i=0, node_ptr=node_record_table_ptr; i<node_record_count;
	     i++, node_ptr++) {
		info("%s: %u", node_ptr->name, node_ptr->node_rank);
	}
#endif

	return SLURM_SUCCESS;
}

/*
 * topo_get_node_addr - build node address and the associated pattern
 *      based on the topology information
 *
 * example of output :
 *      address : s0.s4.s8.tux1
 *      pattern : switch.switch.switch.node
 */
extern int topo_get_node_addr(char* node_name, char** paddr, char** ppattern)
{
	if (find_node_record(node_name) == NULL)
		return SLURM_ERROR;

	*paddr = xstrdup(node_name);
	*ppattern = xstrdup("node");
	return SLURM_SUCCESS;
}
