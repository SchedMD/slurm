/*****************************************************************************\
 *  gres_filter.c - filters used in the select plugin
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

#include "src/common/slurm_xlator.h"

#include "gres_filter.h"

static uint32_t gpu_plugin_id = NO_VAL, mps_plugin_id = NO_VAL;

static void _job_core_filter(void *job_gres_data, void *node_gres_data,
			     bool use_total_gres, bitstr_t *core_bitmap,
			     int core_start_bit, int core_end_bit,
			     char *node_name,
			     uint32_t plugin_id)
{
	int i, j, core_ctld;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *) job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	bitstr_t *avail_core_bitmap = NULL;
	bool use_busy_dev = false;

	if (!node_gres_ptr->topo_cnt || !core_bitmap ||	/* No topology info */
	    !job_gres_ptr->gres_per_node)		/* No job GRES */
		return;

	if (mps_plugin_id == NO_VAL)
		mps_plugin_id = gres_plugin_build_id("mps");

	if (!use_total_gres &&
	    (plugin_id == mps_plugin_id) &&
	    (node_gres_ptr->gres_cnt_alloc != 0)) {
		/* We must use the ONE already active GRES of this type */
		use_busy_dev = true;
	}

	/* Determine which specific cores can be used */
	avail_core_bitmap = bit_copy(core_bitmap);
	bit_nclear(avail_core_bitmap, core_start_bit, core_end_bit);
	for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
		if (node_gres_ptr->topo_gres_cnt_avail[i] == 0)
			continue;
		if (!use_total_gres &&
		    (node_gres_ptr->topo_gres_cnt_alloc[i] >=
		     node_gres_ptr->topo_gres_cnt_avail[i]))
			continue;
		if (use_busy_dev &&
		    (node_gres_ptr->topo_gres_cnt_alloc[i] == 0))
			continue;
		if (job_gres_ptr->type_name &&
		    (!node_gres_ptr->topo_type_name[i] ||
		     (job_gres_ptr->type_id != node_gres_ptr->topo_type_id[i])))
			continue;
		if (!node_gres_ptr->topo_core_bitmap[i]) {
			FREE_NULL_BITMAP(avail_core_bitmap);	/* No filter */
			return;
		}
		core_ctld = core_end_bit - core_start_bit + 1;
		gres_validate_node_cores(node_gres_ptr, core_ctld, node_name);
		core_ctld = bit_size(node_gres_ptr->topo_core_bitmap[i]);
		for (j = 0; j < core_ctld; j++) {
			if (bit_test(node_gres_ptr->topo_core_bitmap[i], j)) {
				bit_set(avail_core_bitmap, core_start_bit + j);
			}
		}
	}
	bit_and(core_bitmap, avail_core_bitmap);
	FREE_NULL_BITMAP(avail_core_bitmap);
}

/*
 * Clear the core_bitmap for cores which are not usable by this job (i.e. for
 *	cores which are already bound to other jobs or lack GRES)
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *                     gres_plugin_node_config_validate()
 * IN use_total_gres - if set then consider all GRES resources as available,
 *		       and none are commited to running jobs
 * IN/OUT core_bitmap - Identification of available cores
 *                      (NULL if no restriction)
 * IN core_start_bit - index into core_bitmap for this node's first cores
 * IN core_end_bit - index into core_bitmap for this node's last cores
 */
extern void gres_filter_cons_res(List job_gres_list, List node_gres_list,
				 bool use_total_gres,
				 bitstr_t *core_bitmap,
				 int core_start_bit, int core_end_bit,
				 char *node_name)
{
	ListIterator  job_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;

	if ((job_gres_list == NULL) || (core_bitmap == NULL))
		return;
	if (node_gres_list == NULL) {
		bit_nclear(core_bitmap, core_start_bit, core_end_bit);
		return;
	}

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		node_gres_ptr = list_find_first(node_gres_list, gres_find_id,
		                                &job_gres_ptr->plugin_id);
		if (node_gres_ptr == NULL) {
			/* node lack resources required by the job */
			bit_nclear(core_bitmap, core_start_bit, core_end_bit);
			break;
		}

		_job_core_filter(job_gres_ptr->gres_data,
				 node_gres_ptr->gres_data,
				 use_total_gres, core_bitmap,
				 core_start_bit, core_end_bit,
				 node_name,
				 job_gres_ptr->plugin_id);
	}
	list_iterator_destroy(job_gres_iter);

	return;
}

static bool *_build_avail_cores_by_sock(bitstr_t *core_bitmap,
					uint16_t sockets,
					uint16_t cores_per_sock)
{
	bool *avail_cores_by_sock = xcalloc(sockets, sizeof(bool));
	int s, c, i, lim = 0;

	lim = bit_size(core_bitmap);
	for (s = 0; s < sockets; s++) {
		for (c = 0; c < cores_per_sock; c++) {
			i = (s * cores_per_sock) + c;
			if (i >= lim)
				goto fini;	/* should never happen */
			if (bit_test(core_bitmap, i)) {
				avail_cores_by_sock[s] = true;
				break;
			}
		}
	}

fini:	return avail_cores_by_sock;
}

/*
 * Determine which GRES can be used on this node given the available cores.
 *	Filter out unusable GRES.
 * IN sock_gres_list - list of sock_gres_t entries built by
 *                     gres_plugin_job_test2()
 * IN avail_mem - memory available for the job
 * IN max_cpus - maximum CPUs available on this node (limited by specialized
 *               cores and partition CPUs-per-node)
 * IN enforce_binding - GRES must be co-allocated with cores
 * IN core_bitmap - Identification of available cores on this node
 * IN sockets - Count of sockets on the node
 * IN cores_per_sock - Count of cores per socket on this node
 * IN cpus_per_core - Count of CPUs per core on this node
 * IN sock_per_node - sockets requested by job per node or NO_VAL
 * IN task_per_node - tasks requested by job per node or NO_VAL16
 * IN cpus_per_task - Count of CPUs per task
 * IN whole_node - we are requesting the whole node or not
 * OUT avail_gpus - Count of available GPUs on this node
 * OUT near_gpus - Count of GPUs available on sockets with available CPUs
 * RET - 0 if job can use this node, -1 otherwise (some GRES limit prevents use)
 */
extern int gres_filter_remove_unusable(List sock_gres_list,
				       uint64_t avail_mem,
				       uint16_t max_cpus,
				       bool enforce_binding,
				       bitstr_t *core_bitmap,
				       uint16_t sockets,
				       uint16_t cores_per_sock,
				       uint16_t cpus_per_core,
				       uint32_t sock_per_node,
				       uint16_t task_per_node,
				       uint16_t cpus_per_task,
				       bool whole_node,
				       uint16_t *avail_gpus,
				       uint16_t *near_gpus)
{
	ListIterator sock_gres_iter;
	sock_gres_t *sock_gres;
	bool *avail_cores_by_sock = NULL;
	uint64_t max_gres, mem_per_gres = 0, near_gres_cnt = 0;
	uint16_t cpus_per_gres;
	int s, rc = 0;

	*avail_gpus = 0;
	*near_gpus = 0;
	if (!core_bitmap || !sock_gres_list ||
	    (list_count(sock_gres_list) == 0))
		return rc;

	if (gpu_plugin_id == NO_VAL)
		gpu_plugin_id = gres_plugin_build_id("gpu");

	sock_gres_iter = list_iterator_create(sock_gres_list);
	while ((sock_gres = (sock_gres_t *) list_next(sock_gres_iter))) {
		uint64_t min_gres = 1, tmp_u64;
		if (sock_gres->job_specs) {
			gres_job_state_t *job_gres_ptr = sock_gres->job_specs;
			if (whole_node)
				min_gres = sock_gres->total_cnt;
			else if (job_gres_ptr->gres_per_node)
				min_gres = job_gres_ptr-> gres_per_node;
			if (job_gres_ptr->gres_per_socket) {
				tmp_u64 = job_gres_ptr->gres_per_socket;
				if (sock_per_node != NO_VAL)
					tmp_u64 *= sock_per_node;
				min_gres = MAX(min_gres, tmp_u64);
			}
			if (job_gres_ptr->gres_per_task) {
				tmp_u64 = job_gres_ptr->gres_per_task;
				if (task_per_node != NO_VAL16)
					tmp_u64 *= task_per_node;
				min_gres = MAX(min_gres, tmp_u64);
			}
		}
		if (!sock_gres->job_specs)
			cpus_per_gres = 0;
		else if (sock_gres->job_specs->cpus_per_gres)
			cpus_per_gres = sock_gres->job_specs->cpus_per_gres;
		else if (sock_gres->job_specs->ntasks_per_gres &&
			 (sock_gres->job_specs->ntasks_per_gres != NO_VAL16))
			cpus_per_gres = sock_gres->job_specs->ntasks_per_gres *
				cpus_per_task;
		else
			cpus_per_gres = sock_gres->job_specs->def_cpus_per_gres;
		if (cpus_per_gres) {
			max_gres = max_cpus / cpus_per_gres;
			if ((max_gres == 0) ||
			    (sock_gres->job_specs->gres_per_node > max_gres) ||
			    (sock_gres->job_specs->gres_per_task > max_gres) ||
			    (sock_gres->job_specs->gres_per_socket > max_gres)){
				log_flag(GRES, "%s: Insufficient CPUs for any GRES: max_gres (%"PRIu64") = max_cpus (%d) / cpus_per_gres (%d)",
					 __func__, max_gres, max_cpus,
					 cpus_per_gres);
				rc = -1;
				break;
			}
		}
		if (!sock_gres->job_specs)
			mem_per_gres = 0;
		else if (sock_gres->job_specs->mem_per_gres)
			mem_per_gres = sock_gres->job_specs->mem_per_gres;
		else
			mem_per_gres = sock_gres->job_specs->def_mem_per_gres;
		if (mem_per_gres && avail_mem) {
			if (mem_per_gres <= avail_mem) {
				sock_gres->max_node_gres = avail_mem /
							   mem_per_gres;
			} else {
				log_flag(GRES, "%s: Insufficient memory for any GRES: mem_per_gres (%"PRIu64") > avail_mem (%"PRIu64")",
					 __func__, mem_per_gres, avail_mem);
				rc = -1;
				break;
			}
		}
		if (sock_gres->cnt_by_sock || enforce_binding) {
			if (!avail_cores_by_sock) {
				avail_cores_by_sock =_build_avail_cores_by_sock(
							core_bitmap, sockets,
							cores_per_sock);
			}
		}
		/*
		 * NOTE: gres_per_socket enforcement is performed by
		 * _build_sock_gres_by_topo(), called by gres_plugin_job_test2()
		 */
		if (sock_gres->cnt_by_sock && enforce_binding) {
			for (s = 0; s < sockets; s++) {
				if (avail_cores_by_sock[s] == 0) {
					sock_gres->total_cnt -=
						sock_gres->cnt_by_sock[s];
					sock_gres->cnt_by_sock[s] = 0;
				}
			}
			near_gres_cnt = sock_gres->total_cnt;
		} else if (sock_gres->cnt_by_sock) { /* NO enforce_binding */
			near_gres_cnt = sock_gres->total_cnt;
			for (s = 0; s < sockets; s++) {
				if (avail_cores_by_sock[s] == 0) {
					near_gres_cnt -=
						sock_gres->cnt_by_sock[s];
				}
			}
		} else {
			near_gres_cnt = sock_gres->total_cnt;
		}
		if (sock_gres->job_specs && !whole_node &&
		    sock_gres->job_specs->gres_per_node) {
			if ((sock_gres->max_node_gres == 0) ||
			    (sock_gres->max_node_gres >
			     sock_gres->job_specs->gres_per_node)) {
				sock_gres->max_node_gres =
					sock_gres->job_specs->gres_per_node;
			}
		}
		/* Avoid max_node_gres with ntasks_per_gres and whole node */
		if (cpus_per_gres &&
		    ((sock_gres->job_specs->ntasks_per_gres == NO_VAL16) ||
		     !whole_node)) {
			int cpu_cnt;
			cpu_cnt = bit_set_count(core_bitmap);
			cpu_cnt *= cpus_per_core;
			max_gres = cpu_cnt / cpus_per_gres;
			if (max_gres == 0) {
				log_flag(GRES, "%s: max_gres == 0 == cpu_cnt (%d) / cpus_per_gres (%d)",
					 __func__, cpu_cnt, cpus_per_gres);
				rc = -1;
				break;
			} else if ((sock_gres->max_node_gres == 0) ||
				   (sock_gres->max_node_gres > max_gres)) {
				sock_gres->max_node_gres = max_gres;
			}
		}
		if (mem_per_gres) {
			max_gres = avail_mem / mem_per_gres;
			sock_gres->total_cnt = MIN(sock_gres->total_cnt,
						   max_gres);
		}
		if ((sock_gres->total_cnt < min_gres) ||
		    ((sock_gres->max_node_gres != 0) &&
		     (sock_gres->max_node_gres < min_gres))) {
			log_flag(GRES, "%s: min_gres (%"PRIu64") is > max_node_gres (%"PRIu64") or sock_gres->total_cnt (%"PRIu64")",
				 __func__, min_gres, sock_gres->max_node_gres,
				 sock_gres->total_cnt);
			rc = -1;
			break;
		}

		if (sock_gres->plugin_id == gpu_plugin_id) {
			 *avail_gpus += sock_gres->total_cnt;
			if (sock_gres->max_node_gres &&
			    (sock_gres->max_node_gres < near_gres_cnt))
				near_gres_cnt = sock_gres->max_node_gres;
			if (*near_gpus < 0xff)	/* avoid overflow */
				*near_gpus += near_gres_cnt;
		}
	}
	list_iterator_destroy(sock_gres_iter);
	xfree(avail_cores_by_sock);

	return rc;
}
