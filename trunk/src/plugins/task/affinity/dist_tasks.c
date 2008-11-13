/*****************************************************************************\
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

static int _task_layout_lllp_block(launch_tasks_request_msg_t *req,
				   uint32_t node_id, bitstr_t ***masks_p);
static int _task_layout_lllp_cyclic(launch_tasks_request_msg_t *req,
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
 * lllp_distribution
 *
 * Note: lllp stands for Lowest Level of Logical Processors. 
 *
 * When automatic binding is enabled:
 *      - no binding flags set >= CPU_BIND_NONE, and
 *      - a auto binding level selected CPU_BIND_TO_{SOCKETS,CORES,THREADS}
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
        const uint32_t *gtid = req->global_task_ids[(int)node_id];
	
	slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
	if(req->cpu_bind_type >= CPU_BIND_NONE) {
		info("lllp_distribution jobid [%u] manual binding: %s",
		     req->job_id, buf_type);
		return;
	}
	if (!((req->cpu_bind_type & CPU_BIND_TO_THREADS) ||
	      (req->cpu_bind_type & CPU_BIND_TO_CORES) ||
	      (req->cpu_bind_type & CPU_BIND_TO_SOCKETS) ||
	      (req->cpu_bind_type & CPU_BIND_TO_LDOMS))) {
		info("lllp_distribution jobid [%u] auto binding off: %s",
		     req->job_id, buf_type);
		return;
	}

	/* We are still thinking about this. Does this make sense?
	if (req->task_dist == SLURM_DIST_ARBITRARY) {
		req->cpu_bind_type >= CPU_BIND_NONE;
		info("lllp_distribution jobid [%u] -m hostfile - auto binding off ",
		     req->job_id);
		return;
	}
	*/

	info("lllp_distribution jobid [%u] auto binding: %s, dist %d",
	     req->job_id, buf_type, req->task_dist);

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
		rc = _task_layout_lllp_cyclic(req, node_id, &masks); 
		req->task_dist = SLURM_DIST_BLOCK_CYCLIC;
		break;
	}


	/* FIXME: think about core_bitmap for select/linear (all '1')
	 * I'm also worried about core_bitmap with CPU_BIND_TO_SOCKETS &
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
	}
	_lllp_free_masks(maxtasks, masks);
}


/* helper function for _get_avail_map
 *
 * IN: req         - launch request structure
 * IN: job_node_id - index of the local node in the job allocation
 * IN/OUT: sockets - pointer to socket count variable
 * IN/OUT: cores   - pointer to cores_per_socket count variable
 * OUT:  returns the core_bitmap index of the first core for this node
 */
static int _get_local_node_info(slurm_cred_arg_t *arg, uint32_t job_node_id,
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

/* enforce max_sockets, max_cores */
static void _enforce_limits(launch_tasks_request_msg_t *req, bitstr_t *mask,
			    uint16_t hw_sockets, uint16_t hw_cores,
			    uint16_t hw_threads)
{
	uint16_t i, j, size, count = 0;
	int prev = -1;

	size = bit_size(mask);
	/* enforce max_sockets */
	for (i = 0; i < size; i++) {
		if (bit_test(mask, i) == 0)
			continue;
		/* j = first bit in socket; i = last bit in socket */
		j = i/(hw_cores * hw_threads) * (hw_cores * hw_threads);
		i = j+(hw_cores * hw_threads)-1;
		if (++count > req->max_sockets) {
			bit_nclear(mask, j, i);
			count--;
		}
	}
	/* enforce max_cores */
	for (i = 0; i < size; i++) {
		if (bit_test(mask, i) == 0)
			continue;
		/* j = first bit in socket */
		j = i/(hw_cores * hw_threads) * (hw_cores * hw_threads);
		if (j != prev) {
			/* we're in a new socket, so reset the count */
			count = 0;
			prev = j;
		}
		/* j = first bit in core; i = last bit in core */
		j = i/hw_threads * hw_threads;
		i = j+hw_threads-1;
		if (++count > req->max_cores) {
			bit_nclear(mask, j, i);
			count--;
		}
	}
}

static bitstr_t *_get_avail_map(launch_tasks_request_msg_t *req,
				uint16_t *hw_sockets, uint16_t *hw_cores,
				uint16_t *hw_threads)
{
	bitstr_t *req_map, *hw_map;
	slurm_cred_arg_t arg;
	uint16_t c, s, t, num_threads, sockets, cores, hw_size;
	uint32_t job_node_id;
	int start;
	char *str;

	*hw_sockets = conf->sockets;
	*hw_cores   = conf->cores;
	*hw_threads = conf->threads;
	hw_size    = (*hw_sockets) * (*hw_cores) * (*hw_threads);

	if (slurm_cred_get_args(req->cred, &arg) != SLURM_SUCCESS)
		return NULL;

	/* we need this node's ID in relation to the whole
	 * job allocation, not just this jobstep */
	job_node_id = nodelist_find(arg.job_hostlist, conf->node_name);
	start = _get_local_node_info(&arg, job_node_id, &sockets, &cores);
	if (start < 0)
		return NULL;
	debug3("task/affinity: slurmctld s %u c %u; hw s %u c %u t %u",
		sockets, cores, *hw_sockets, *hw_cores, *hw_threads);

	req_map = (bitstr_t *) bit_alloc(sockets*cores);
	hw_map  = (bitstr_t *) bit_alloc(hw_size);
	if (!req_map || !hw_map) {
		bit_free(req_map);
		bit_free(hw_map);
		return NULL;
	}
	/* transfer core_bitmap data to local req_map */
	for (c = 0; c < sockets * cores; c++)
		if (bit_test(arg.core_bitmap, start+c))
			bit_set(req_map, c);

	str = (char *)bit_fmt_hexmask(req_map);
	debug3("task/affinity: job %u.%u CPU mask from slurmctld: %s",
		req->job_id, req->job_step_id, str);
	xfree(str);

	/* first pass at mapping req_map to hw_map */
	num_threads = MIN(req->max_threads, (*hw_threads));
	for (s = 0; s < sockets; s++) {
		/* for first pass, keep socket index within hw limits */
		s %= (*hw_sockets);
		for (c = 0; c < cores; c++) {
			/* for first pass, keep core index within hw limits */
			c %= (*hw_cores) * (*hw_threads);
			if (bit_test(req_map, s*cores+c) == 0)
				continue;
			/* core_bitmap does not include threads, so we
			 * add them here but limit them to what the job
			 * requested */
			for (t = 0; t < num_threads; t++) {
				uint16_t bit =  s*(*hw_cores)*(*hw_threads) +
						c*(*hw_threads) + t;
				if (bit_test(hw_map, bit) == 0) {
					bit_set(hw_map, bit);
					bit_clear(req_map, s*cores+c);
				}
			}
		}
	}
	/* second pass: place any remaining bits. Remaining bits indicate
	 *		an out-of-sync configuration between the slurmctld
	 *		and the local node.
	 */
	start = bit_set_count(req_map);
	if (start > 0) {
		debug3("task/affinity: placing remaining %d bits anywhere",
			start);
	}
	str = (char *)bit_fmt_hexmask(hw_map);
	debug3("task/affinity: job %u.%u CPU mask for local node: %s",
		req->job_id, req->job_step_id, str);
	xfree(str);

	/* enforce max_sockets and max_cores limits */
	_enforce_limits(req, hw_map, *hw_sockets, *hw_cores, *hw_threads);
	
	str = (char *)bit_fmt_hexmask(hw_map);
	debug3("task/affinity: job %u.%u CPU final mask for local node: %s",
		req->job_id, req->job_step_id, str);
	xfree(str);

	bit_free(req_map);
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
	uint16_t num_threads, num_cores, num_sockets;
	int size, maxtasks = req->tasks_to_launch[(int)node_id];
	bitstr_t *avail_map;
	bitstr_t **masks = NULL;
	
	info ("_task_layout_lllp_cyclic ");

	avail_map = _get_avail_map(req, &hw_sockets, &hw_cores, &hw_threads);
	if (!avail_map)
		return SLURM_ERROR;
	
	*masks_p = xmalloc(maxtasks * sizeof(bitstr_t*));
	masks = *masks_p;
	
	size = bit_set_count(avail_map);
	if (!size || size < req->cpus_per_task) {
		error("task/affinity: no set bits in avail_map!");
		bit_free(avail_map);
		return SLURM_ERROR;
	}
	
	size = bit_size(avail_map);
	num_sockets = MIN(req->max_sockets, hw_sockets);
	num_cores   = MIN(req->max_cores, hw_cores);
	num_threads = MIN(req->max_threads, hw_threads);
	i = 0;
	while (taskcount < maxtasks) {
		if (taskcount == last_taskcount) {
			fatal("_task_layout_lllp_cyclic failure");
		}
		last_taskcount = taskcount; 
		for (t = 0; t < num_threads; t++) {
			for (c = 0; c < hw_cores; c++) {
				for (s = 0; s < hw_sockets; s++) {
					uint16_t bit = s*(hw_cores*hw_threads) +
							c*(hw_threads) + t;
					if (bit_test(avail_map, bit) == 0)
						continue;
					if (masks[taskcount] == NULL)
						masks[taskcount] =
						    (bitstr_t *)bit_alloc(size);
					bit_set(masks[taskcount], bit);
					if (++i < req->cpus_per_task)
						continue;
					i = 0;
					if (++taskcount >= maxtasks)
						break;
				}
				if (taskcount >= maxtasks)
					break;
			}
			if (taskcount >= maxtasks)
				break;
		}
	}
	bit_free(avail_map);
	
	/* last step: expand the masks to bind each task
	 * to the requested resource */
	_expand_masks(req->cpu_bind_type, maxtasks, masks,
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
	uint16_t num_sockets, num_cores, num_threads;
	int maxtasks = req->tasks_to_launch[(int)node_id];
	int *task_array;
	bitstr_t *avail_map;
	bitstr_t **masks = NULL;

	info("_task_layout_lllp_block ");

	avail_map = _get_avail_map(req, &hw_sockets, &hw_cores, &hw_threads);
	if (!avail_map) {
		return SLURM_ERROR;
	}

	size = bit_set_count(avail_map);
	if (!size || size < req->cpus_per_task) {
		error("task/affinity: no set bits in avail_map!");
		bit_free(avail_map);
		return SLURM_ERROR;
	}
	size = bit_size(avail_map);

	*masks_p = xmalloc(maxtasks * sizeof(bitstr_t*));
	masks = *masks_p;

	task_array = xmalloc(size * sizeof(int));
	if (!task_array) {
		error("In lllp_block: task_array memory error");
		bit_free(avail_map);
		return SLURM_ERROR;
	}
	
	/* block distribution with oversubsciption */
	num_sockets = MIN(req->max_sockets, hw_sockets);
	num_cores   = MIN(req->max_cores, hw_cores);
	num_threads = MIN(req->max_threads, hw_threads);
	c = 0;
	while(taskcount < maxtasks) {
		if (taskcount == last_taskcount) {
			fatal("_task_layout_lllp_block infinite loop");
		}
		last_taskcount = taskcount;
		/* the abstract map is already laid out in block order,
		 * so just iterate over it
		 */
		for (i = 0; i < size; i++) {
			/* skip unrequested threads */
			if (i%hw_threads >= num_threads)
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
			if (++taskcount >= maxtasks)
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
				masks[taskcount] = (bitstr_t *)bit_alloc(size);
			bit_set(masks[taskcount++], i);
		}
	}
	/* now set additional CPUs for cpus_per_task > 1 */
	for (t=0; t<maxtasks && req->cpus_per_task>1; t++) {
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
	_expand_masks(req->cpu_bind_type, maxtasks, masks,
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
			bit_set(newmask, bit);
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

	slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
	info("_lllp_generate_cpu_bind jobid [%u]: %s, %s",
	     req->job_id, buf_type, masks_str);
}

