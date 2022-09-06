/*****************************************************************************\
 *  node_data.c - Functions dealing with structures dealing with nodes unique to
 *                the select plugin.
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC
 *  Derived in large part from select/cons_[res|tres] plugins
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

#include "cons_common.h"

node_use_record_t *select_node_usage  = NULL;

/* Delete the given select_node_usage array */
extern void node_data_destroy(node_use_record_t *node_usage)
{
	int i;

	if (node_usage) {
		for (i = 0; next_node(&i); i++) {
			FREE_NULL_LIST(node_usage[i].gres_list);
		}
		xfree(node_usage);
	}
}

extern void node_data_dump(void)
{
	node_record_t *node_ptr;
	List gres_list;
	int i;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE))
		return;

	for (i = 0; (node_ptr = next_node(&i)); i++) {
		info("Node:%s Boards:%u SocketsPerBoard:%u CoresPerSocket:%u ThreadsPerCore:%u TotalCores:%u CumeCores:%u TotalCPUs:%u PUsPerCore:%u AvailMem:%"PRIu64" AllocMem:%"PRIu64" State:%s(%d)",
		     node_ptr->name,
		     node_ptr->boards,
		     node_ptr->tot_sockets / node_ptr->boards,
		     node_ptr->cores,
		     node_ptr->threads,
		     node_ptr->tot_cores,
		     cr_get_coremap_offset(i + 1),
		     node_ptr->cpus,
		     node_ptr->tpc,
		     node_ptr->real_memory,
		     select_node_usage[node_ptr->index].alloc_memory,
		     common_node_state_str(
			     select_node_usage[node_ptr->index].node_state),
		     select_node_usage[node_ptr->index].node_state);

		if (select_node_usage[node_ptr->index].gres_list)
			gres_list = select_node_usage[node_ptr->index].
					gres_list;
		else
			gres_list = node_ptr->gres_list;
		if (gres_list)
			gres_node_state_log(gres_list, node_ptr->name);
	}
}

/* Create a duplicate node_use_record list */
extern node_use_record_t *node_data_dup_use(
	node_use_record_t *orig_ptr, bitstr_t *node_map)
{
	node_use_record_t *new_use_ptr, *new_ptr;
	node_record_t *node_ptr;
	List gres_list;

	if (orig_ptr == NULL)
		return NULL;

	new_use_ptr = xcalloc(node_record_count, sizeof(node_use_record_t));
	new_ptr = new_use_ptr;

	for (int i = 0;
	     (node_ptr =
	      (node_map ? next_node_bitmap(node_map, &i) : next_node(&i)));
	     i++) {
		new_ptr[i].node_state   = orig_ptr[i].node_state;
		new_ptr[i].alloc_memory = orig_ptr[i].alloc_memory;
		if (orig_ptr[i].gres_list)
			gres_list = orig_ptr[i].gres_list;
		else
			gres_list = node_ptr->gres_list;
		new_ptr[i].gres_list = gres_node_state_list_dup(gres_list);
	}
	return new_use_ptr;
}
