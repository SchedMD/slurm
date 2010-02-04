/*****************************************************************************\
 *  Copyright (C) 2006-2009 Hewlett-Packard Development Company, L.P.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
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

#include "affinity.h"
#include "dist_tasks.h"
#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/xmalloc.h"
#include "src/slurmd/slurmd/slurmd.h"

#ifdef HAVE_NUMA
#include <numa.h>
#endif

static char *_alloc_mask(launch_tasks_request_msg_t *req,
			 int *whole_node_cnt, int *whole_socket_cnt,
			 int *whole_core_cnt, int *whole_thread_cnt,
			 int *part_socket_cnt, int *part_core_cnt);
static bitstr_t *_get_avail_map(launch_tasks_request_msg_t *req,
				uint16_t *hw_sockets, uint16_t *hw_cores,
				uint16_t *hw_threads);
static int _get_local_node_info(slurm_cred_arg_t *arg, int job_node_id,
				uint16_t *sockets, uint16_t *cores);

static int _task_layout_lllp_block(launch_tasks_request_msg_t *req,
				   uint32_t node_id, bitstr_t ***masks_p);
static int _task_layout_lllp_cyclic(launch_tasks_request_msg_t *req,
				    uint32_t node_id, bitstr_t ***masks_p);
static int _task_layout_lllp_multi(launch_tasks_request_msg_t *req,
				    uint32_t node_id, bitstr_t ***masks_p);

static void _lllp_map_abstract_masks(const uint32_t maxtasks,
				     bitstr_t **masks);
static void _lllp_generate_cpu_bind(launch_tasks_request_msg_t *req,
				    const uint32_t maxtasks,
				    bitstr_t **masks);

/*     BLOCK_MAP     physical machine LLLP index to abstract block LLLP index
 *     BLOCK_MAP_INV physical abstract block LLLP index to machine LLLP index
 */
#define BLOCK_MAP(index)	_block_map(index, conf->block_map)
#define BLOCK_MAP_INV(index)	_block_map(index, conf->block_map_inv)


/* _block_map
 *
 * safely returns a mapped index using a provided block map
 *
 * IN - index to map
 * IN - map to use
 */
static uint16_t _block_map(uint16_t index, uint16_t *map)
{
	if (map == NULL) {
	    	return index;
	}
	/* make sure bit falls in map */
	if (index >= conf->block_map_size) {
		debug3("wrapping index %u into block_map_size of %u",
		       index, conf->block_map_size);
		index = index % conf->block_map_size;
	}
	index = map[index];
	return(index);
}

static void _task_layout_display_masks(launch_tasks_request_msg_t *req,
					const uint32_t *gtid,
					const uint32_t maxtasks,
					bitstr_t **masks)
{
	int i;
	char *str = NULL;
	for(i = 0; i < maxtasks; i++) {
		str = (char *)bit_fmt_hexmask(masks[i]);
		debug3("_task_layout_display_masks jobid [%u:%d] %s",
		       req->job_id, gtid[i], str);
		xfree(str);
	}
}

static void _lllp_free_masks(const uint32_t maxtasks, bitstr_t **masks)
{
    	int i;
	bitstr_t *bitmask;
	for (i = 0; i < maxtasks; i++) {
		bitmask = masks[i];
	    	if (bitmask) {
			bit_free(bitmask);
		}
	}
	xfree(masks);
}

#ifdef HAVE_NUMA
/* _match_mask_to_ldom
 *
 * expand each mask to encompass the whole locality domain
 * within which it currently exists
 * NOTE: this assumes that the masks are already in logical
 * (and not abstract) CPU order.
 */
static void _match_masks_to_ldom(const uint32_t maxtasks, bitstr_t **masks)
{
	uint32_t i, b, size;

	if (!masks || !masks[0])
		return;
	size = bit_size(masks[0]);
	for(i = 0; i < maxtasks; i++) {
		for (b = 0; b < size; b++) {
			if (bit_test(masks[i], b)) {
				/* get the NUMA node for this CPU, and then
				 * set all CPUs in the mask that exist in
				 * the same CPU */
				int c;
				uint16_t nnid = slurm_get_numa_node(b);
				for (c = 0; c < size; c++) {
					if (slurm_get_numa_node(c) == nnid)
						bit_set(masks[i], c);
				}
			}
		}
	}
}
#endif

/*
 * batch_bind - Set the batch request message so as to bind the shell to the
 *	proper resources
 */
void batch_bind(batch_job_launch_msg_t *req)
{
	bitstr_t *req_map, *hw_map;
	slurm_cred_arg_t arg;
	uint16_t sockets=0, cores=0, num_cpus;
	int start, p, t, task_cnt=0;
	char *str;

	if (slurm_cred_get_args(req->cred, &arg) != SLURM_SUCCESS) {
		error("task/affinity: job lacks a credential");
		return;
	}
	start = _get_local_node_info(&arg, 0, &sockets, &cores);
	if (start != 0) {
		error("task/affinity: missing node 0 in job credential");
		slurm_cred_free_args(&arg);
		return;
	}

	num_cpus  = MIN((sockets * cores),
			 (conf->sockets * conf->cores));
	req_map = (bitstr_t *) bit_alloc(num_cpus);
	hw_map  = (bitstr_t *) bit_alloc(conf->block_map_size);
	if (!req_map || !hw_map) {
		error("task/affinity: malloc error");
		if (req_map)
			bit_free(req_map);
		if (hw_map)
			bit_free(hw_map);
		slurm_cred_free_args(&arg);
		return;
	}

	/* Transfer core_bitmap data to local req_map.
	 * The MOD function handles the case where fewer processes
	 * physically exist than are configured (slurmd is out of
	 * sync with the slurmctld daemon). */
	for (p = 0; p < (sockets * cores); p++) {
		if (bit_test(arg.core_bitmap, p))
			bit_set(req_map, (p % num_cpus));
	}
	str = (char *)bit_fmt_hexmask(req_map);
	debug3("task/affinity: job %u CPU mask from slurmctld: %s",
		req->job_id, str);
	xfree(str);

	for (p = 0; p < num_cpus; p++) {
		if (bit_test(req_map, p) == 0)
			continue;
		/* core_bitmap does not include threads, so we
		 * add them here but limit them to what the job
		 * requested */
		for (t = 0; t < conf->threads; t++) {
			uint16_t pos = p * conf->threads + t;
			if (pos >= conf->block_map_size) {
				info("more resources configured than exist");
				p = num_cpus;
				break;
			}
			bit_set(hw_map, pos);
			task_cnt++;
		}
	}
	if (task_cnt) {
		req->cpu_bind_type = CPU_BIND_MASK;
		if (conf->task_plugin_param & CPU_BIND_VERBOSE)
			req->cpu_bind_type |= CPU_BIND_VERBOSE;
		req->cpu_bind = (char *)bit_fmt_hexmask(hw_map);
		info("task/affinity: job %u CPU input mask for node: %s",
		     req->job_id, req->cpu_bind);
		/* translate abstract masks to actual hardware layout */
		_lllp_map_abstract_masks(1, &hw_map);
#ifdef HAVE_NUMA
		if (req->cpu_bind_type & CPU_BIND_TO_LDOMS) {
			_match_masks_to_ldom(1, &hw_map);
		}
#endif
		xfree(req->cpu_bind);
		req->cpu_bind = (char *)bit_fmt_hexmask(hw_map);
		info("task/affinity: job %u CPU final HW mask for node: %s",
		     req->job_id, req->cpu_bind);
	} else {
		error("task/affinity: job %u allocated no CPUs",
		      req->job_id);
	}
	bit_free(hw_map);
	bit_free(req_map);
	slurm_cred_free_args(&arg);
}

/*
 * lllp_distribution
 *
 * Note: lllp stands for Lowest Level of Logical Processors.
 *
 * When automatic binding is enabled:
 *      - no binding flags set >= CPU_BIND_NONE, and
 *      - a auto binding level selected CPU_BIND_TO_{SOCKETS,CORES,THREADS}
 * Otherwise limit job step to the allocated CPUs
 *
 * generate the appropriate cpu_bind type and string which results in
 * the specified lllp distribution.
 *
 * IN/OUT- job launch request (cpu_bind_type and cpu_bind updated)
 * IN- global task id array
 */
void lllp_distribution(launch_tasks_request_msg_t *req, uint32_t node_id)
{
	int rc = SLURM_SUCCESS;
	bitstr_t **masks = NULL;
	char buf_type[100];
	int maxtasks = req->tasks_to_launch[(int)node_id];
	int whole_nodes, whole_sockets, whole_cores, whole_threads;
	int part_sockets, part_cores;
        const uint32_t *gtid = req->global_task_ids[(int)node_id];
	static uint16_t bind_entity = CPU_BIND_TO_THREADS | CPU_BIND_TO_CORES |
				      CPU_BIND_TO_SOCKETS | CPU_BIND_TO_LDOMS;
	static uint16_t bind_mode = CPU_BIND_NONE   | CPU_BIND_MASK   |
				    CPU_BIND_RANK   | CPU_BIND_MAP    |
				    CPU_BIND_LDMASK | CPU_BIND_LDRANK |
				    CPU_BIND_LDMAP;

	if (req->cpu_bind_type & bind_mode) {
		/* Explicit step binding specified by user */
		char *avail_mask = _alloc_mask(req,
					       &whole_nodes,  &whole_sockets,
					       &whole_cores,  &whole_threads,
					       &part_sockets, &part_cores);
		if ((whole_nodes == 0) && avail_mask) {
			/* Step does NOT have access to whole node,
			 * bind to full mask of available processors */
			xfree(req->cpu_bind);
			req->cpu_bind = avail_mask;
			req->cpu_bind_type &= (~bind_mode);
			req->cpu_bind_type |= CPU_BIND_MASK;
		} else {
			/* Step does have access to whole node,
			 * bind to whatever step wants */
			xfree(avail_mask);
		}
		slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
		info("lllp_distribution jobid [%u] manual binding: %s",
		     req->job_id, buf_type);
		return;
	}

	if (!(req->cpu_bind_type & bind_entity)) {
		/* No bind unit (sockets, cores) specified by user,
		 * pick something reasonable */
		int max_tasks = req->tasks_to_launch[(int)node_id];
		char *avail_mask = _alloc_mask(req,
					       &whole_nodes,  &whole_sockets,
					       &whole_cores,  &whole_threads,
					       &part_sockets, &part_cores);
		debug("binding tasks:%d to "
		      "nodes:%d sockets:%d:%d cores:%d:%d threads:%d",
		      max_tasks, whole_nodes, whole_sockets ,part_sockets,
		      whole_cores, part_cores, whole_threads);
		if ((max_tasks == whole_sockets) && (part_sockets == 0)) {
			req->cpu_bind_type |= CPU_BIND_TO_SOCKETS;
			goto make_auto;
		}
		if ((max_tasks == whole_cores) && (part_cores == 0)) {
			req->cpu_bind_type |= CPU_BIND_TO_CORES;
			goto make_auto;
		}
		if (max_tasks == whole_threads) {
			req->cpu_bind_type |= CPU_BIND_TO_THREADS;
			goto make_auto;
		}
		if (avail_mask) {
			xfree(req->cpu_bind);
			req->cpu_bind = avail_mask;
			req->cpu_bind_type |= CPU_BIND_MASK;
		}
		slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
		info("lllp_distribution jobid [%u] auto binding off: %s",
		     req->job_id, buf_type);
		return;

  make_auto:	xfree(avail_mask);
		slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
		info("lllp_distribution jobid [%u] implicit auto binding: "
		     "%s, dist %d", req->job_id, buf_type, req->task_dist);
	} else {
		/* Explicit bind unit (sockets, cores) specified by user */
		slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
		info("lllp_distribution jobid [%u] binding: %s, dist %d",
		     req->job_id, buf_type, req->task_dist);
	}

	switch (req->task_dist) {
	case SLURM_DIST_BLOCK_BLOCK:
	case SLURM_DIST_CYCLIC_BLOCK:
	case SLURM_DIST_PLANE:
		/* tasks are distributed in blocks within a plane */
		rc = _task_layout_lllp_block(req, node_id, &masks);
		break;
	case SLURM_DIST_CYCLIC:
	case SLURM_DIST_BLOCK:
	case SLURM_DIST_CYCLIC_CYCLIC:
	case SLURM_DIST_BLOCK_CYCLIC:
		rc = _task_layout_lllp_cyclic(req, node_id, &masks);
		break;
	default:
		if (req->cpus_per_task > 1)
			rc = _task_layout_lllp_multi(req, node_id, &masks);
		else
			rc = _task_layout_lllp_cyclic(req, node_id, &masks);
		req->task_dist = SLURM_DIST_BLOCK_CYCLIC;
		break;
	}

	/* FIXME: I'm worried about core_bitmap with CPU_BIND_TO_SOCKETS &
	 * max_cores - does select/cons_res plugin allocate whole
	 * socket??? Maybe not. Check srun man page.
	 */

	if (rc == SLURM_SUCCESS) {
		_task_layout_display_masks(req, gtid, maxtasks, masks);
	    	/* translate abstract masks to actual hardware layout */
		_lllp_map_abstract_masks(maxtasks, masks);
		_task_layout_display_masks(req, gtid, maxtasks, masks);
#ifdef HAVE_NUMA
		if (req->cpu_bind_type & CPU_BIND_TO_LDOMS) {
			_match_masks_to_ldom(maxtasks, masks);
			_task_layout_display_masks(req, gtid, maxtasks, masks);
		}
#endif
	    	 /* convert masks into cpu_bind mask string */
		 _lllp_generate_cpu_bind(req, maxtasks, masks);
	} else {
		char *avail_mask = _alloc_mask(req,
					       &whole_nodes,  &whole_sockets,
					       &whole_cores,  &whole_threads,
					       &part_sockets, &part_cores);
		if (avail_mask) {
			xfree(req->cpu_bind);
			req->cpu_bind = avail_mask;
			req->cpu_bind_type &= (~bind_mode);
			req->cpu_bind_type |= CPU_BIND_MASK;
		}
		slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
		error("lllp_distribution jobid [%u] overriding binding: %s",
		      req->job_id, buf_type);
		error("Verify socket/core/thread counts in configuration");
	}
	_lllp_free_masks(maxtasks, masks);
}


/*
 * _get_local_node_info - get job allocation details for this node
 * IN: req         - launch request structure
 * IN: job_node_id - index of the local node in the job allocation
 * IN/OUT: sockets - pointer to socket count variable
 * IN/OUT: cores   - pointer to cores_per_socket count variable
 * OUT:  returns the core_bitmap index of the first core for this node
 */
static int _get_local_node_info(slurm_cred_arg_t *arg, int job_node_id,
				uint16_t *sockets, uint16_t *cores)
{
	int bit_start = 0, bit_finish = 0;
	int i, index = -1, cur_node_id = -1;

	do {
		index++;
		for (i = 0; i < arg->sock_core_rep_count[index] &&
			     cur_node_id < job_node_id; i++) {
			bit_start = bit_finish;
			bit_finish += arg->sockets_per_node[index] *
					arg->cores_per_socket[index];
			cur_node_id++;
		}

	} while (cur_node_id < job_node_id);

	*sockets = arg->sockets_per_node[index];
	*cores   = arg->cores_per_socket[index];
	return bit_start;
}

/* Determine which CPUs a job step can use.
 * OUT whole_<entity>_count - returns count of whole <entities> in this
 *                            allocation for this node
 * OUT part__<entity>_count - returns count of partial <entities> in this
 *                            allocation for this node
 * RET - a string representation of the available mask or NULL on error
 * NOTE: Caller must xfree() the return value. */
static char *_alloc_mask(launch_tasks_request_msg_t *req,
			 int *whole_node_cnt,  int *whole_socket_cnt,
			 int *whole_core_cnt,  int *whole_thread_cnt,
			 int *part_socket_cnt, int *part_core_cnt)
{
	uint16_t sockets, cores, threads;
	int c, s, t, i;
	int c_miss, s_miss, t_miss, c_hit, t_hit;
	bitstr_t *alloc_bitmap;
	char *str_mask;
	bitstr_t *alloc_mask;

	*whole_node_cnt   = 0;
	*whole_socket_cnt = 0;
	*whole_core_cnt   = 0;
	*whole_thread_cnt = 0;
	*part_socket_cnt  = 0;
	*part_core_cnt    = 0;

	alloc_bitmap = _get_avail_map(req, &sockets, &cores, &threads);
	if (!alloc_bitmap)
		return NULL;

	alloc_mask = bit_alloc(bit_size(alloc_bitmap));
	if (!alloc_mask) {
		error("malloc error");
		bit_free(alloc_bitmap);
		return NULL;
	}

	i = 0;
	for (s=0, s_miss=false; s<sockets; s++) {
		for (c=0, c_hit=c_miss=false; c<cores; c++) {
			for (t=0, t_hit=t_miss=false; t<threads; t++) {
				if (bit_test(alloc_bitmap, i)) {
					bit_set(alloc_mask, i);
					(*whole_thread_cnt)++;
					t_hit = true;
					c_hit = true;
				} else
					t_miss = true;
				i++;
			}
			if (!t_miss)
				(*whole_core_cnt)++;
			else {
				if (t_hit)
					(*part_core_cnt)++;
				c_miss = true;
			}
		}
		if (!c_miss)
			(*whole_socket_cnt)++;
		else {
			if (c_hit)
				(*part_socket_cnt)++;
			s_miss = true;
		}
	}
	if (!s_miss)
		(*whole_node_cnt)++;
	bit_free(alloc_bitmap);

	/* translate abstract masks to actual hardware layout */
	_lllp_map_abstract_masks(1, &alloc_mask);

#ifdef HAVE_NUMA
	if (req->cpu_bind_type & CPU_BIND_TO_LDOMS) {
		_match_masks_to_ldom(1, &alloc_mask);
	}
#endif

	str_mask = bit_fmt_hexmask(alloc_mask);
	bit_free(alloc_mask);
	return str_mask;
}

/*
 * Given a job step request, return an equivalent local bitmap for this node
 * IN req          - The job step launch request
 * OUT hw_sockets  - number of actual sockets on this node
 * OUT hw_cores    - number of actual cores per socket on this node
 * OUT hw_threads  - number of actual threads per core on this node
 * RET: bitmap of processors available to this job step on this node
 *      OR NULL on error
 */
static bitstr_t *_get_avail_map(launch_tasks_request_msg_t *req,
				uint16_t *hw_sockets, uint16_t *hw_cores,
				uint16_t *hw_threads)
{
	bitstr_t *req_map, *hw_map;
	slurm_cred_arg_t arg;
	uint16_t p, t, num_cpus, sockets, cores;
	int job_node_id;
	int start;
	char *str;

	*hw_sockets = conf->sockets;
	*hw_cores   = conf->cores;
	*hw_threads = conf->threads;

	if (slurm_cred_get_args(req->cred, &arg) != SLURM_SUCCESS) {
		error("task/affinity: job lacks a credential");
		return NULL;
	}

	/* we need this node's ID in relation to the whole
	 * job allocation, not just this jobstep */
	job_node_id = nodelist_find(arg.job_hostlist, conf->node_name);
	start = _get_local_node_info(&arg, job_node_id, &sockets, &cores);
	if (start < 0) {
		error("task/affinity: missing node %d in job credential",
		      job_node_id);
		slurm_cred_free_args(&arg);
		return NULL;
	}
	debug3("task/affinity: slurmctld s %u c %u; hw s %u c %u t %u",
		sockets, cores, *hw_sockets, *hw_cores, *hw_threads);

	num_cpus   = MIN((sockets * cores),
			  ((*hw_sockets)*(*hw_cores)));
	req_map = (bitstr_t *) bit_alloc(num_cpus);
	hw_map  = (bitstr_t *) bit_alloc(conf->block_map_size);
	if (!req_map || !hw_map) {
		error("task/affinity: malloc error");
		bit_free(req_map);
		bit_free(hw_map);
		slurm_cred_free_args(&arg);
		return NULL;
	}
	/* Transfer core_bitmap data to local req_map.
	 * The MOD function handles the case where fewer processes
	 * physically exist than are configured (slurmd is out of
	 * sync with the slurmctld daemon). */
	for (p = 0; p < (sockets * cores); p++) {
		if (bit_test(arg.core_bitmap, start+p))
			bit_set(req_map, (p % num_cpus));
	}

	str = (char *)bit_fmt_hexmask(req_map);
	debug3("task/affinity: job %u.%u CPU mask from slurmctld: %s",
		req->job_id, req->job_step_id, str);
	xfree(str);

	for (p = 0; p < num_cpus; p++) {
		if (bit_test(req_map, p) == 0)
			continue;
		/* core_bitmap does not include threads, so we
		 * add them here but limit them to what the job
		 * requested */
		for (t = 0; t < (*hw_threads); t++) {
			uint16_t bit = p * (*hw_threads) + t;
			bit_set(hw_map, bit);
		}
	}

	str = (char *)bit_fmt_hexmask(hw_map);
	debug3("task/affinity: job %u.%u CPU final mask for local node: %s",
		req->job_id, req->job_step_id, str);
	xfree(str);

	bit_free(req_map);
	slurm_cred_free_args(&arg);
	return hw_map;
}

/* helper function for _expand_masks() */
static void _blot_mask(bitstr_t *mask, uint16_t blot)
{
	uint16_t i, size = 0;
	int prev = -1;

	if (!mask)
		return;
	size = bit_size(mask);
	for (i = 0; i < size; i++) {
		if (bit_test(mask, i)) {
			/* fill in this blot */
			uint16_t start = (i / blot) * blot;
			if (start != prev) {
				bit_nset(mask, start, start+blot-1);
				prev = start;
			}
		}
	}
}

/* foreach mask, expand the mask around the set bits to include the
 * complete resource to which the set bits are to be bound */
static void _expand_masks(uint16_t cpu_bind_type, const uint32_t maxtasks,
			  bitstr_t **masks, uint16_t hw_sockets,
			  uint16_t hw_cores, uint16_t hw_threads)
{
	uint32_t i;

	if (cpu_bind_type & CPU_BIND_TO_THREADS)
		return;
	if (cpu_bind_type & CPU_BIND_TO_CORES) {
		if (hw_threads < 2)
			return;
		for (i = 0; i < maxtasks; i++) {
			_blot_mask(masks[i], hw_threads);
		}
		return;
	}
	if (cpu_bind_type & CPU_BIND_TO_SOCKETS) {
		if (hw_threads*hw_cores < 2)
			return;
		for (i = 0; i < maxtasks; i++) {
			_blot_mask(masks[i], hw_threads*hw_cores);
		}
		return;
	}
}

/*
 * _task_layout_lllp_multi
 *
 * A variant of _task_layout_lllp_cyclic for use with allocations having
 * more than one CPU per task, put the tasks as close as possible (fill
 * core rather than going next socket for the extra task)
 *
 */
static int _task_layout_lllp_multi(launch_tasks_request_msg_t *req,
				    uint32_t node_id, bitstr_t ***masks_p)
{
	int last_taskcount = -1, taskcount = 0;
	uint16_t c, i, s, t, hw_sockets = 0, hw_cores = 0, hw_threads = 0;
	int size, max_tasks = req->tasks_to_launch[(int)node_id];
	int max_cpus = max_tasks * req->cpus_per_task;
	bitstr_t *avail_map;
	bitstr_t **masks = NULL;

	info ("_task_layout_lllp_multi ");

	avail_map = _get_avail_map(req, &hw_sockets, &hw_cores, &hw_threads);
	if (!avail_map)
		return SLURM_ERROR;

	*masks_p = xmalloc(max_tasks * sizeof(bitstr_t*));
	masks = *masks_p;

	size = bit_set_count(avail_map);
	if (size < max_tasks) {
		error("task/affinity: only %d bits in avail_map for %d tasks!",
		      size, max_tasks);
		bit_free(avail_map);
		return SLURM_ERROR;
	}
	if (size < max_cpus) {
		/* Possible result of overcommit */
		i = size / max_tasks;
		info("task/affinity: reset cpus_per_task from %d to %d",
		     req->cpus_per_task, i);
		req->cpus_per_task = i;
	}

	size = bit_size(avail_map);
	i = 0;
	while (taskcount < max_tasks) {
		if (taskcount == last_taskcount)
			fatal("_task_layout_lllp_multi failure");
		last_taskcount = taskcount;
		for (s = 0; s < hw_sockets; s++) {
			for (c = 0; c < hw_cores; c++) {
				for (t = 0; t < hw_threads; t++) {
					uint16_t bit = s*(hw_cores*hw_threads) +
							c*(hw_threads) + t;
					if (bit_test(avail_map, bit) == 0)
						continue;
					if (masks[taskcount] == NULL)
						masks[taskcount] =
							bit_alloc(conf->block_map_size);
					bit_set(masks[taskcount], bit);
					if (++i < req->cpus_per_task)
						continue;
					i = 0;
					if (++taskcount >= max_tasks)
						break;
				}
				if (taskcount >= max_tasks)
					break;
			}
			if (taskcount >= max_tasks)
				break;
		}
	}
	bit_free(avail_map);

	/* last step: expand the masks to bind each task
	 * to the requested resource */
	_expand_masks(req->cpu_bind_type, max_tasks, masks,
			hw_sockets, hw_cores, hw_threads);

	return SLURM_SUCCESS;
}

/*
 * _task_layout_lllp_cyclic
 *
 * task_layout_lllp_cyclic creates a cyclic distribution at the
 * lowest level of logical processor which is either socket, core or
 * thread depending on the system architecture. The Cyclic algorithm
 * is the same as the the Cyclic distribution performed in srun.
 *
 *  Distribution at the lllp:
 *  -m hostfile|plane|block|cyclic:block|cyclic
 *
 * The first distribution "hostfile|plane|block|cyclic" is computed
 * in srun. The second distribution "plane|block|cyclic" is computed
 * locally by each slurmd.
 *
 * The input to the lllp distribution algorithms is the gids (tasks
 * ids) generated for the local node.
 *
 * The output is a mapping of the gids onto logical processors
 * (thread/core/socket) with is expressed cpu_bind masks.
 *
 */
static int _task_layout_lllp_cyclic(launch_tasks_request_msg_t *req,
				    uint32_t node_id, bitstr_t ***masks_p)
{
	int last_taskcount = -1, taskcount = 0;
	uint16_t c, i, s, t, hw_sockets = 0, hw_cores = 0, hw_threads = 0;
	int size, max_tasks = req->tasks_to_launch[(int)node_id];
	int max_cpus = max_tasks * req->cpus_per_task;
	bitstr_t *avail_map;
	bitstr_t **masks = NULL;

	info ("_task_layout_lllp_cyclic ");

	avail_map = _get_avail_map(req, &hw_sockets, &hw_cores, &hw_threads);
	if (!avail_map)
		return SLURM_ERROR;

	*masks_p = xmalloc(max_tasks * sizeof(bitstr_t*));
	masks = *masks_p;

	size = bit_set_count(avail_map);
	if (size < max_tasks) {
		error("task/affinity: only %d bits in avail_map for %d tasks!",
		      size, max_tasks);
		bit_free(avail_map);
		return SLURM_ERROR;
	}
	if (size < max_cpus) {
		/* Possible result of overcommit */
		i = size / max_tasks;
		info("task/affinity: reset cpus_per_task from %d to %d",
		     req->cpus_per_task, i);
		req->cpus_per_task = i;
	}

	size = bit_size(avail_map);
	i = 0;
	while (taskcount < max_tasks) {
		if (taskcount == last_taskcount)
			fatal("_task_layout_lllp_cyclic failure");
		last_taskcount = taskcount;
		for (t = 0; t < hw_threads; t++) {
			for (c = 0; c < hw_cores; c++) {
				for (s = 0; s < hw_sockets; s++) {
					uint16_t bit = s*(hw_cores*hw_threads) +
							c*(hw_threads) + t;
					if (bit_test(avail_map, bit) == 0)
						continue;
					if (masks[taskcount] == NULL)
						masks[taskcount] =
						    (bitstr_t *)bit_alloc(conf->block_map_size);
					bit_set(masks[taskcount], bit);
					if (++i < req->cpus_per_task)
						continue;
					i = 0;
					if (++taskcount >= max_tasks)
						break;
				}
				if (taskcount >= max_tasks)
					break;
			}
			if (taskcount >= max_tasks)
				break;
		}
	}
	bit_free(avail_map);

	/* last step: expand the masks to bind each task
	 * to the requested resource */
	_expand_masks(req->cpu_bind_type, max_tasks, masks,
			hw_sockets, hw_cores, hw_threads);

	return SLURM_SUCCESS;
}

/*
 * _task_layout_lllp_block
 *
 * task_layout_lllp_block will create a block distribution at the
 * lowest level of logical processor which is either socket, core or
 * thread depending on the system architecture. The Block algorithm
 * is the same as the the Block distribution performed in srun.
 *
 *  Distribution at the lllp:
 *  -m hostfile|plane|block|cyclic:block|cyclic
 *
 * The first distribution "hostfile|plane|block|cyclic" is computed
 * in srun. The second distribution "plane|block|cyclic" is computed
 * locally by each slurmd.
 *
 * The input to the lllp distribution algorithms is the gids (tasks
 * ids) generated for the local node.
 *
 * The output is a mapping of the gids onto logical processors
 * (thread/core/socket)  with is expressed cpu_bind masks.
 *
 */
static int _task_layout_lllp_block(launch_tasks_request_msg_t *req,
				   uint32_t node_id, bitstr_t ***masks_p)
{
	int c, i, j, t, size, last_taskcount = -1, taskcount = 0;
	uint16_t hw_sockets = 0, hw_cores = 0, hw_threads = 0;
	int max_tasks = req->tasks_to_launch[(int)node_id];
	int max_cpus = max_tasks * req->cpus_per_task;
	int *task_array;
	bitstr_t *avail_map;
	bitstr_t **masks = NULL;

	info("_task_layout_lllp_block ");

	avail_map = _get_avail_map(req, &hw_sockets, &hw_cores, &hw_threads);
	if (!avail_map) {
		return SLURM_ERROR;
	}

	size = bit_set_count(avail_map);
	if (size < max_tasks) {
		error("task/affinity: only %d bits in avail_map for %d tasks!",
		      size, max_tasks);
		bit_free(avail_map);
		return SLURM_ERROR;
	}
	if (size < max_cpus) {
		/* Possible result of overcommit */
		i = size / max_tasks;
		info("task/affinity: reset cpus_per_task from %d to %d",
		     req->cpus_per_task, i);
		req->cpus_per_task = i;
	}
	size = bit_size(avail_map);

	*masks_p = xmalloc(max_tasks * sizeof(bitstr_t*));
	masks = *masks_p;

	task_array = xmalloc(size * sizeof(int));
	if (!task_array) {
		error("In lllp_block: task_array memory error");
		bit_free(avail_map);
		return SLURM_ERROR;
	}

	/* block distribution with oversubsciption */
	c = 0;
	while(taskcount < max_tasks) {
		if (taskcount == last_taskcount) {
			fatal("_task_layout_lllp_block infinite loop");
		}
		last_taskcount = taskcount;
		/* the abstract map is already laid out in block order,
		 * so just iterate over it
		 */
		for (i = 0; i < size; i++) {
			/* skip unrequested threads */
			if (i%hw_threads >= hw_threads)
				continue;
			/* skip unavailable resources */
			if (bit_test(avail_map, i) == 0)
				continue;
			/* if multiple CPUs per task, only
			 * count the task on the first CPU */
			if (c == 0)
				task_array[i] += 1;
			if (++c < req->cpus_per_task)
				continue;
			c = 0;
			if (++taskcount >= max_tasks)
				break;
		}
	}
	/* Distribute the tasks and create per-task masks that only
	 * contain the first CPU. Note that unused resources
	 * (task_array[i] == 0) will get skipped */
	taskcount = 0;
	for (i = 0; i < size; i++) {
		for (t = 0; t < task_array[i]; t++) {
			if (masks[taskcount] == NULL)
				masks[taskcount] = (bitstr_t *)bit_alloc(conf->block_map_size);
			bit_set(masks[taskcount++], i);
		}
	}
	/* now set additional CPUs for cpus_per_task > 1 */
	for (t=0; t<max_tasks && req->cpus_per_task>1; t++) {
		if (!masks[t])
			continue;
		for (i = 0; i < size; i++) {
			if (bit_test(masks[t], i) == 0)
				continue;
			for (j=i+1,c=1; j<size && c<req->cpus_per_task;j++) {
				if (bit_test(avail_map, j) == 0)
					continue;
				bit_set(masks[t], j);
				c++;
			}
			if (c < req->cpus_per_task) {
				/* we haven't found all of the CPUs for this
				 * task, so we'll wrap the search to cover the
				 * whole node */
				for (j=0; j<i && c<req->cpus_per_task; j++) {
					if (bit_test(avail_map, j) == 0)
						continue;
					bit_set(masks[t], j);
					c++;
				}
			}
		}
	}

	xfree(task_array);
	bit_free(avail_map);

	/* last step: expand the masks to bind each task
	 * to the requested resource */
	_expand_masks(req->cpu_bind_type, max_tasks, masks,
			hw_sockets, hw_cores, hw_threads);

	return SLURM_SUCCESS;
}

/*
 * _lllp_map_abstract_mask
 *
 * Map one abstract block mask to a physical machine mask
 *
 * IN - mask to map
 * OUT - mapped mask (storage allocated in this routine)
 */
static bitstr_t *_lllp_map_abstract_mask(bitstr_t *bitmask)
{
    	int i, bit;
	int num_bits = bit_size(bitmask);
	bitstr_t *newmask = NULL;
	newmask = (bitstr_t *) bit_alloc(num_bits);

	/* remap to physical machine */
	for (i = 0; i < num_bits; i++) {
		if (bit_test(bitmask,i)) {
			bit = BLOCK_MAP(i);
			if(bit < bit_size(newmask))
				bit_set(newmask, bit);
			else
				error("_lllp_map_abstract_mask: can't go from "
				      "%d -> %d since we only have %d bits",
				      i, bit, bit_size(newmask));
		}
	}
	return newmask;
}

/*
 * _lllp_map_abstract_masks
 *
 * Map an array of abstract block masks to physical machine masks
 *
 * IN- maximum number of tasks
 * IN/OUT- array of masks
 */
static void _lllp_map_abstract_masks(const uint32_t maxtasks, bitstr_t **masks)
{
    	int i;
	debug3("_lllp_map_abstract_masks");

	for (i = 0; i < maxtasks; i++) {
		bitstr_t *bitmask = masks[i];
	    	if (bitmask) {
			bitstr_t *newmask = _lllp_map_abstract_mask(bitmask);
			bit_free(bitmask);
			masks[i] = newmask;
		}
	}
}

/*
 * _lllp_generate_cpu_bind
 *
 * Generate the cpu_bind type and string given an array of bitstr_t masks
 *
 * IN/OUT- job launch request (cpu_bind_type and cpu_bind updated)
 * IN- maximum number of tasks
 * IN- array of masks
 */
static void _lllp_generate_cpu_bind(launch_tasks_request_msg_t *req,
				    const uint32_t maxtasks, bitstr_t **masks)
{
    	int i, num_bits=0, masks_len;
	bitstr_t *bitmask;
	bitoff_t charsize;
	char *masks_str = NULL;
	char buf_type[100];

	for (i = 0; i < maxtasks; i++) {
		bitmask = masks[i];
	    	if (bitmask) {
			num_bits = bit_size(bitmask);
			break;
		}
	}
	charsize = (num_bits + 3) / 4;		/* ASCII hex digits */
	charsize += 3;				/* "0x" and trailing "," */
	masks_len = maxtasks * charsize + 1;	/* number of masks + null */

	debug3("_lllp_generate_cpu_bind %d %d %d", maxtasks, charsize,
		masks_len);

	masks_str = xmalloc(masks_len);
	masks_len = 0;
	for (i = 0; i < maxtasks; i++) {
	    	char *str;
		int curlen;
		bitmask = masks[i];
	    	if (bitmask == NULL) {
			continue;
		}
		str = (char *)bit_fmt_hexmask(bitmask);
		curlen = strlen(str) + 1;

		if (masks_len > 0)
			masks_str[masks_len-1]=',';
		strncpy(&masks_str[masks_len], str, curlen);
		masks_len += curlen;
		xassert(masks_str[masks_len] == '\0');
		xfree(str);
	}

	if (req->cpu_bind) {
	    	xfree(req->cpu_bind);
	}
	if (masks_str[0] != '\0') {
		req->cpu_bind = masks_str;
		req->cpu_bind_type |= CPU_BIND_MASK;
	} else {
		req->cpu_bind = NULL;
		req->cpu_bind_type &= ~CPU_BIND_VERBOSE;
	}

	/* clear mask generation bits */
	req->cpu_bind_type &= ~CPU_BIND_TO_THREADS;
	req->cpu_bind_type &= ~CPU_BIND_TO_CORES;
	req->cpu_bind_type &= ~CPU_BIND_TO_SOCKETS;
	req->cpu_bind_type &= ~CPU_BIND_TO_LDOMS;

	slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
	info("_lllp_generate_cpu_bind jobid [%u]: %s, %s",
	     req->job_id, buf_type, masks_str);
}

