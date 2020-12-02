/*****************************************************************************\
 *  gres_ctld.c - Functions for gres used only in the slurmctld
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Derived in large part from code previously in common/gres.c
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

#include "gres_ctld.h"
#include "src/common/xstring.h"

static void _job_select_whole_node_internal(
	gres_key_t *job_search_key, gres_node_state_t *node_state_ptr,
	int type_inx, char *gres_name, List job_gres_list)
{
	gres_state_t *job_gres_ptr;
	gres_job_state_t *job_state_ptr;

	if (!(job_gres_ptr = list_find_first(job_gres_list,
					     gres_find_job_by_key,
					     job_search_key))) {
		job_state_ptr = xmalloc(sizeof(gres_job_state_t));

		job_gres_ptr = xmalloc(sizeof(gres_state_t));
		job_gres_ptr->plugin_id = job_search_key->plugin_id;
		job_gres_ptr->gres_data = job_state_ptr;
		job_state_ptr->gres_name = xstrdup(gres_name);
		if (type_inx != -1)
			job_state_ptr->type_name =
				xstrdup(node_state_ptr->type_name[type_inx]);
		job_state_ptr->type_id = job_search_key->type_id;

		list_append(job_gres_list, job_gres_ptr);
	} else
		job_state_ptr = job_gres_ptr->gres_data;

	/*
	 * Add the total_gres here but no count, that will be done after
	 * allocation.
	 */
	if (node_state_ptr->no_consume) {
		job_state_ptr->total_gres = NO_CONSUME_VAL64;
	} else if (type_inx != -1)
		job_state_ptr->total_gres +=
			node_state_ptr->type_cnt_avail[type_inx];
	else
		job_state_ptr->total_gres += node_state_ptr->gres_cnt_avail;
}

/*
 * Fill in job_gres_list with the total amount of GRES on a node.
 * OUT job_gres_list - This list will be destroyed and remade with all GRES on
 *                     node.
 * IN node_gres_list - node's gres_list built by
 *		       gres_plugin_node_config_validate()
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * RET SLURM_SUCCESS or error code
 */
extern int gres_ctld_job_select_whole_node(
	List *job_gres_list, List node_gres_list,
	uint32_t job_id, char *node_name)
{
	ListIterator node_gres_iter;
	gres_state_t *node_gres_ptr;
	gres_node_state_t *node_state_ptr;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL) {
		error("%s: job %u has gres specification while node %s has none",
		      __func__, job_id, node_name);
		return SLURM_ERROR;
	}

	if (!*job_gres_list)
		*job_gres_list = list_create(gres_job_list_delete);

	node_gres_iter = list_iterator_create(node_gres_list);
	while ((node_gres_ptr = list_next(node_gres_iter))) {
		char *gres_name;
		gres_key_t job_search_key;
		node_state_ptr = (gres_node_state_t *) node_gres_ptr->gres_data;

		/*
		 * Don't check for no_consume here, we need them added here and
		 * will filter them out in gres_plugin_job_alloc_whole_node()
		 */
		if (!node_state_ptr->gres_cnt_config)
			continue;

		if (!(gres_name = gres_get_name_from_id(
			      node_gres_ptr->plugin_id))) {
			error("%s: no plugin configured for data type %u for job %u and node %s",
			      __func__, node_gres_ptr->plugin_id, job_id,
			      node_name);
			/* A likely sign that GresPlugins has changed */
			continue;
		}

		job_search_key.plugin_id = node_gres_ptr->plugin_id;

		if (!node_state_ptr->type_cnt) {
			job_search_key.type_id = 0;
			_job_select_whole_node_internal(
				&job_search_key, node_state_ptr,
				-1, gres_name, *job_gres_list);
		} else {
			for (int j = 0; j < node_state_ptr->type_cnt; j++) {
				job_search_key.type_id = gres_plugin_build_id(
					node_state_ptr->type_name[j]);
				_job_select_whole_node_internal(
					&job_search_key, node_state_ptr,
					j, gres_name, *job_gres_list);
			}
		}
		xfree(gres_name);
	}
	list_iterator_destroy(node_gres_iter);

	return SLURM_SUCCESS;
}
