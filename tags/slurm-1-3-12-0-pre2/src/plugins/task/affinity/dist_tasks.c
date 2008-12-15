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

#include <limits.h>       /* INT_MAX */
#include "src/plugins/task/affinity/dist_tasks.h"

static slurm_lllp_ctx_t *lllp_ctx = NULL;	/* binding context */
static struct node_gids *lllp_tasks = NULL;	/* Keep track of the task count
						 * for logical processors
						 * socket/core/thread. */
static uint32_t lllp_reserved_size = 0;		/* lllp reserved array size */
static uint32_t *lllp_reserved = NULL;   	/* count of Reserved lllps 
						 * (socket, core, threads) */


static void _task_layout_display_masks(launch_tasks_request_msg_t *req,
				       const uint32_t *gtid,
				       const uint32_t maxtasks,
				       bitstr_t **masks);
static int _init_lllp(void);
static int _cleanup_lllp(void);
static void _print_tasks_per_lllp(void);
static int _task_layout_lllp_block(launch_tasks_request_msg_t *req,
				   const uint32_t *gtid,
				   const uint32_t maxtasks,
				   bitstr_t ***masks_p);
static int _task_layout_lllp_cyclic(launch_tasks_request_msg_t *req,
				    const uint32_t *gtid,
				    const uint32_t maxtasks,
				    bitstr_t ***masks_p);
static int _task_layout_lllp_plane(launch_tasks_request_msg_t *req,
				   const uint32_t *gtid,
				   const uint32_t maxtasks,
				   bitstr_t ***masks_p);
static void _lllp_enlarge_masks(launch_tasks_request_msg_t *req,
				const uint32_t maxtasks,
				bitstr_t **masks);
static void _lllp_use_available(launch_tasks_request_msg_t *req,
				const uint32_t maxtasks,
				bitstr_t **masks);
static bitstr_t *_lllp_map_abstract_mask (bitstr_t *bitmask);
static void _lllp_map_abstract_masks(const uint32_t maxtasks,
				     bitstr_t **masks);
static void _lllp_generate_cpu_bind(launch_tasks_request_msg_t *req,
				    const uint32_t maxtasks,
				    bitstr_t **masks);
static void _lllp_free_masks(launch_tasks_request_msg_t *req,
			     const uint32_t maxtasks,
			     bitstr_t **masks);
static void _single_mask(const uint16_t nsockets, 
			 const uint16_t ncores, 
			 const uint16_t nthreads, 
			 const uint16_t socket_id,
			 const uint16_t core_id, 
			 const uint16_t thread_id,
			 const bool bind_to_exact_socket,
			 const bool bind_to_exact_core,
			 const bool bind_to_exact_thread,
			 bitstr_t ** single_mask);
static void _get_resources_this_node(uint16_t *cpus,
				     uint16_t *sockets,
				     uint16_t *cores,
				     uint16_t *threads,
				     uint16_t *alloc_cores,
				     uint32_t jobid);
static void _cr_update_reservation(int reserve, uint32_t *reserved, 
				   bitstr_t *mask);

/* Convenience macros: 
 *     SCT_TO_LLLP   sockets cores threads to abstract block LLLP index
 */
#define SCT_TO_LLLP(s,c,t,ncores,nthreads)			\
	(s)*((ncores)*(nthreads)) + (c)*(nthreads) + (t)
/*     BLOCK_MAP     physical machine LLLP index to abstract block LLLP index
 *     BLOCK_MAP_INV physical abstract block LLLP index to machine LLLP index
 */
#define BLOCK_MAP(index)	_block_map(index, conf->block_map)
#define BLOCK_MAP_INV(index)	_block_map(index, conf->block_map_inv)

static uint16_t _block_map(uint16_t index, uint16_t *map);

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
 * the sepcified lllp distribution.
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
	      (req->cpu_bind_type & CPU_BIND_TO_SOCKETS))) {
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
		rc = _task_layout_lllp_block(req, gtid, maxtasks, &masks);
		break;
	case SLURM_DIST_CYCLIC:
	case SLURM_DIST_BLOCK:
	case SLURM_DIST_CYCLIC_CYCLIC:
	case SLURM_DIST_BLOCK_CYCLIC:
		rc = _task_layout_lllp_cyclic(req, gtid, maxtasks, &masks); 
		break;
	case SLURM_DIST_PLANE:
		rc = _task_layout_lllp_plane(req, gtid, maxtasks, &masks); 
		break;
	default:
		rc = _task_layout_lllp_cyclic(req, gtid, maxtasks, &masks); 
		req->task_dist = SLURM_DIST_BLOCK_CYCLIC;
		break;
	}

	if (rc == SLURM_SUCCESS) {
		_task_layout_display_masks(req, gtid, maxtasks, masks); 
		if (req->cpus_per_task > 1) {
			_lllp_enlarge_masks(req, maxtasks, masks);
		}
		_task_layout_display_masks(req, gtid, maxtasks, masks); 
	    	_lllp_use_available(req, maxtasks, masks);
		_task_layout_display_masks(req, gtid, maxtasks, masks); 
	    	_lllp_map_abstract_masks(maxtasks, masks);
		_task_layout_display_masks(req, gtid, maxtasks, masks); 
	    	_lllp_generate_cpu_bind(req, maxtasks, masks);
	}
	_lllp_free_masks(req, maxtasks, masks);
}

static
void _task_layout_display_masks(launch_tasks_request_msg_t *req, 
				const uint32_t *gtid,
				const uint32_t maxtasks,
				bitstr_t **masks)
{
	int i;
	for(i=0; i<maxtasks;i++) {
		char *str = bit_fmt_hexmask(masks[i]);
		debug3("_task_layout_display_masks jobid [%u:%d] %s",
		       req->job_id, gtid[i], str);
		xfree(str);
	}
}

/*
 * _compute_min_overlap
 *
 * Given a mask and a set of current reservations, return the
 * minimum overlap between the mask and the reservations and the
 * rotation required to obtain it
 *
 * IN-  bitmask - bitmask to rotate
 * IN-  resv - current reservations
 * IN-  rotmask_size - size of mask to use during rotation
 * IN-  rotval - starting rotation value
 * IN-  rot_incr - rotation increment
 * OUT- p_min_overlap- minimum overlap
 * OUT- p_min_rotval- rotation to obtain minimum overlap
 */
static void
_compute_min_overlap(bitstr_t *bitmask, uint32_t *resv,
			int rotmask_size, int rotval, int rot_incr, 
			int *p_min_overlap, int *p_min_rotval)
{
	int min_overlap = INT_MAX;
	int min_rotval  = 0;
	int rot_cnt;
	int j;
	if (rot_incr <= 0) {
		rot_incr = 1;
	}
	rot_cnt = rotmask_size / rot_incr;
	debug3("  rotval:%d rot_incr:%d rot_cnt:%d",
					rotval, rot_incr, rot_cnt);
	for (j = 0; j < rot_cnt; j++) {
		int overlap;		       
		bitstr_t *newmask = bit_rotate_copy(bitmask, rotval,
						    rotmask_size);
		bitstr_t *physmask = _lllp_map_abstract_mask(newmask);
		overlap = int_and_set_count((int *)resv,
					    lllp_reserved_size,
					    physmask);
		bit_free(newmask);
		bit_free(physmask);
		debug3("  rotation #%d %d => overlap:%d", j, rotval, overlap);
		if (overlap < min_overlap) {
			min_overlap = overlap;
			min_rotval  = rotval;
		}
		if (overlap == 0) {	/* no overlap, stop rotating */
			debug3("  --- found zero overlap, stopping search");
			break;
		}
		rotval += rot_incr;
	}
	debug3("  min_overlap:%d min_rotval:%d",
					min_overlap, min_rotval);
	*p_min_overlap = min_overlap;
	*p_min_rotval  = min_rotval;
}

/*
 * _lllp_enlarge_masks
 *
 * Given an array of masks, update the masks to honor the number
 * of cpus requested per task in req->cpus_per_task.  Note: not
 * concerned with mask overlap between tasks as _lllp_use_available
 * will take care of that.
 *
 * IN- job launch request
 * IN- maximum number of tasks
 * IN/OUT- array of masks
 */
static void _lllp_enlarge_masks (launch_tasks_request_msg_t *req,
				const uint32_t maxtasks,
				bitstr_t **masks)
{
	int i, j, k, l;
	int cpus_per_task = req->cpus_per_task;

	debug3("_lllp_enlarge_masks");

	/* enlarge each mask */
	for (i = 0; i < maxtasks; i++) {
		bitstr_t *bitmask = masks[i];
		bitstr_t *addmask;
		int bitmask_size = bit_size(bitmask);
		int num_added = 0;

		/* get current number of set bits in bitmask */
		int num_set = bit_set_count(bitmask);
		if (num_set >= cpus_per_task) {
			continue;
		}

		/* add bits by selecting disjoint cores first, then threads */
		for (j = conf->threads; j > 0; j--) {
			/* rotate current bitmask to find new candidate bits */
		        for (k = 1; k < bitmask_size / j; k++) {
				addmask = bit_rotate_copy(bitmask, k*j,
								bitmask_size);

			    	/* check candidate bits to add into to bitmask */
				for (l = 0; l < bitmask_size; l++) {
					if (bit_test(addmask,l) &&
					    !bit_test(bitmask,l)) {
						bit_set(bitmask,l);
						num_set++;
						num_added++;
					}
					if (num_set >= cpus_per_task) {
						break;
					}
				}

				/* done with candidate mask */
				bit_free(addmask);
				if (num_set >= cpus_per_task) {
					break;
				}
			}
			if (num_set >= cpus_per_task) {
				break;
			}
		}
		debug3("  mask %d => added %d bits", i, num_added);
	}
}

/*
 * _lllp_use_available
 *
 * Given an array of masks, update the masks to make best use of
 * available resources based on the current state of reservations
 * recorded in conf->lllp_reserved.
 *
 * IN- job launch request
 * IN- maximum number of tasks
 * IN/OUT- array of masks
 */
static void _lllp_use_available (launch_tasks_request_msg_t *req,
				const uint32_t maxtasks,
				bitstr_t **masks)
{
	int resv_incr, i;
	uint32_t *resv;
	int rotval, prev_rotval;

	/* select the unit of reservation rotation increment based on CR type */
	if ((conf->cr_type == CR_SOCKET) 
	    || (conf->cr_type == CR_SOCKET_MEMORY)) {
		resv_incr = conf->cores * conf->threads; /* socket contents */
	} else if ((conf->cr_type == CR_CORE) 
		   || (conf->cr_type == CR_CORE_MEMORY)) {
		resv_incr = conf->threads;		 /* core contents */
	} else {
		resv_incr = conf->threads;		 /* core contents */
	}
	if (resv_incr < 1) {		/* make sure increment is non-zero */ 
		debug3("_lllp_use_available changed resv_incr %d to 1", resv_incr);
		resv_incr = 1;
	}

	debug3("_lllp_use_available resv_incr = %d", resv_incr);

	/* get a copy of the current reservations */
	resv = xmalloc(lllp_reserved_size * sizeof(uint32_t));
        memcpy(resv, lllp_reserved, lllp_reserved_size * sizeof(uint32_t));

	/* check each mask against current reservations */
	rotval      = 0;
	prev_rotval = 0;
	for (i = 0; i < maxtasks; i++) {
		bitstr_t *bitmask = masks[i];
		bitstr_t *physmask = NULL;
		int min_overlap, min_rotval;

		/* create masks that are at least as large as the reservation */
		int bitmask_size = bit_size(bitmask);
		int rotmask_size = MAX(bitmask_size, lllp_reserved_size);

		/* get maximum number of contiguous bits in bitmask */
		int contig_bits = bit_nset_max_count(bitmask);

		/* make sure the reservation increment is larger than the number
		 * of contiguous bits in the mask to maintain any properties
		 * present in the mask (e.g. use both cores on one socket)
		 */
		int this_resv_incr = resv_incr;
		while (this_resv_incr < contig_bits) {
			this_resv_incr += resv_incr;
		}

		/* rotate mask to find the minimum reservation overlap starting
		 * with the previous rotation value
		 */
		rotval  = prev_rotval;
		debug3("mask %d compute_min_overlap contig:%d", i, contig_bits);
		_compute_min_overlap(bitmask, resv,
					rotmask_size, rotval, this_resv_incr,
					&min_overlap, &min_rotval);

		/* if we didn't find a zero overlap, recheck at a thread
		 * granularity
		 */
		if (min_overlap != 0) {
		        int prev_resv_incr = this_resv_incr;
			this_resv_incr = 1;
			if (this_resv_incr != prev_resv_incr) {
				int this_min_overlap, this_min_rotval;
				_compute_min_overlap(bitmask, resv,
					rotmask_size, rotval, this_resv_incr,
					&this_min_overlap, &this_min_rotval);
				if (this_min_overlap < min_overlap) {
					min_overlap = this_min_overlap;
					min_rotval  = this_min_rotval;
				}
			}
		}

		rotval = min_rotval;	/* readjust for the minimum overlap */
		if (rotval != 0) {
			bitstr_t *newmask = bit_rotate_copy(bitmask, rotval,
							    rotmask_size);
			bit_free(masks[i]);
			masks[i] = newmask;
		}

		debug3("  mask %d => rotval %d", i, rotval);
		/* accepted current mask, add to copy of the reservations */
		physmask = _lllp_map_abstract_mask(masks[i]);
		_cr_update_reservation(1, resv, physmask);
		bit_free(physmask);
		prev_rotval = rotval;
	}
	xfree(resv);
}

/*
 * _lllp_map_abstract_mask
 *
 * Map one abstract block mask to a physical machine mask
 *
 * IN - mask to map
 * OUT - mapped mask (storage allocated in this routine)
 */
static bitstr_t *_lllp_map_abstract_mask (bitstr_t *bitmask)
{
    	int i, bit;
	int num_bits = bit_size(bitmask);
	bitstr_t *newmask = bit_alloc(num_bits);

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
static void _lllp_map_abstract_masks(const uint32_t maxtasks,
				     bitstr_t **masks)
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
				    const uint32_t maxtasks,
				    bitstr_t **masks)
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

	debug3("_lllp_generate_cpu_bind %d %d %d", maxtasks, charsize, masks_len);

	masks_str = xmalloc(masks_len);
	masks_len = 0;
	for (i = 0; i < maxtasks; i++) {
	    	char *str;
		int curlen;
		bitmask = masks[i];
	    	if (bitmask == NULL) {
			continue;
		}
		str = bit_fmt_hexmask(bitmask);
		curlen = strlen(str) + 1;

		if (masks_len > 0) masks_str[masks_len-1]=',';
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


static void _lllp_free_masks (launch_tasks_request_msg_t *req,
			      const uint32_t maxtasks,
			      bitstr_t **masks)
{
    	int i;
	bitstr_t *bitmask;
	for (i = 0; i < maxtasks; i++) { 
		bitmask = masks[i];
	    	if (bitmask) {
			bit_free(bitmask);
		}
	}
}

/* 
 * _task_layout_lllp_init performs common initialization required by:
 *	_task_layout_lllp_cyclic
 *	_task_layout_lllp_block
 *	_task_layout_lllp_plane
 */
static int _task_layout_lllp_init(launch_tasks_request_msg_t *req, 
				  const uint32_t maxtasks,
				  bitstr_t ***masks_p,
				  bool *bind_to_exact_socket,
				  bool *bind_to_exact_core,
				  bool *bind_to_exact_thread,
				  uint16_t *usable_cpus,
				  uint16_t *usable_sockets, 
				  uint16_t *usable_cores,
				  uint16_t *usable_threads,
				  uint16_t *hw_sockets, 
				  uint16_t *hw_cores,
				  uint16_t *hw_threads,
				  uint16_t *avail_cpus)
{
	int min_sockets = 1, min_cores = 1;
	uint16_t alloc_cores[conf->sockets];

	if (req->cpu_bind_type & CPU_BIND_TO_THREADS) {
		/* Default: in here in case we decide to change the
		 * default */
		info ("task_layout cpu_bind_type CPU_BIND_TO_THREADS ");
	} else if (req->cpu_bind_type & CPU_BIND_TO_CORES) {
		*bind_to_exact_thread = false;
		info ("task_layout cpu_bind_type CPU_BIND_TO_CORES ");
	} else if (req->cpu_bind_type & CPU_BIND_TO_SOCKETS) {
		*bind_to_exact_thread = false;
		*bind_to_exact_core   = false;
		info ("task_layout cpu_bind_type CPU_BIND_TO_SOCKETS");
	}

	_get_resources_this_node(usable_cpus, usable_sockets, usable_cores,
				 usable_threads, alloc_cores, req->job_id);

	*hw_sockets = *usable_sockets;
	*hw_cores   = *usable_cores;
	*hw_threads = *usable_threads;

	*avail_cpus = slurm_get_avail_procs(req->max_sockets, 
					    req->max_cores, 
					    req->max_threads, 
					    min_sockets,
					    min_cores,
					    req->cpus_per_task,
					    req->ntasks_per_node,
					    req->ntasks_per_socket,
					    req->ntasks_per_core,
					    usable_cpus, usable_sockets,
					    usable_cores, usable_threads,
					    alloc_cores, conf->cr_type,
					    req->job_id, conf->hostname);
	/* Allocate masks array */
	*masks_p = xmalloc(maxtasks * sizeof(bitstr_t*));
	return SLURM_SUCCESS;
}

/* _get_resources_this_node determines counts for already allocated
 * resources (currently sockets and lps) for this node.  
 *
 * Only used when cons_res (Consumable Resources) is enabled with
 * CR_Socket, CR_Cores, or CR_CPU.
 *
 * OUT- Number of allocated sockets on this node
 * OUT- Number of allocated logical processors on this node 
 * 
 */
static void _get_resources_this_node(uint16_t *cpus,
				     uint16_t *sockets,
				     uint16_t *cores,
				     uint16_t *threads,
				     uint16_t *alloc_cores,
	                             uint32_t jobid)
{
	int bit_index = 0;
	int i, j, k;

	/* FIX for heterogeneous socket/core/thread count per system
	 * in future releases */
	*cpus    = conf->cpus;
	*sockets = conf->sockets;
	*cores   = conf->cores;
	*threads = conf->threads;

	for(i = 0; i < *sockets; i++)
		alloc_cores[i] = 0;

	for(i = 0; i < *sockets; i++) {
		for(j = 0; j < *cores; j++) {
			for(k = 0; k < *threads; k++) {
				info("jobid %u lllp_reserved[%d]=%d", jobid, 
				     bit_index, lllp_reserved[bit_index]);
				if(lllp_reserved[bit_index] > 0) {
					if (k == 0) {
						alloc_cores[i]++;
					}
				}
				bit_index++;
			}
		}
	}
		
	xassert(bit_index == (*sockets * *cores * *threads));

#if(0)
	for (i = 0; i < *sockets; i++)
		info("_get_resources jobid:%u hostname:%s socket id:%d cores:%u", 
		     jobid, conf->hostname, i, alloc_cores[i]);
#endif
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
				    const uint32_t *gtid,
				    const uint32_t maxtasks,
				    bitstr_t ***masks_p)
{
	int retval, i, last_taskcount = -1, taskcount = 0, taskid = 0;
	uint16_t socket_index = 0, core_index = 0, thread_index = 0;
	uint16_t hw_sockets = 0, hw_cores = 0, hw_threads = 0;
	uint16_t usable_cpus = 0, avail_cpus = 0;
	uint16_t usable_sockets = 0, usable_cores = 0, usable_threads = 0;
	
	bitstr_t **masks = NULL;
	bool bind_to_exact_socket = true;
	bool bind_to_exact_core   = true;
	bool bind_to_exact_thread = true;
	
	info ("_task_layout_lllp_cyclic ");

	retval = _task_layout_lllp_init(req, maxtasks, masks_p,
					&bind_to_exact_socket, 
					&bind_to_exact_core, 
					&bind_to_exact_thread,
					&usable_cpus, 
					&usable_sockets, 
					&usable_cores, 
					&usable_threads,
					&hw_sockets, 
					&hw_cores, 
					&hw_threads, 
					&avail_cpus);
	if (retval != SLURM_SUCCESS)
		return retval;
	masks = *masks_p;

	for (i=0; taskcount<maxtasks; i++) {
		if (taskcount == last_taskcount) {
			error("_task_layout_lllp_cyclic failure");
			return SLURM_ERROR;
		}
		last_taskcount = taskcount; 
		for (thread_index=0; thread_index<usable_threads; thread_index++) {
			for (core_index=0; core_index<usable_cores; core_index++) {
				for (socket_index=0; socket_index<usable_sockets; 
						     socket_index++) {
					bitstr_t *bitmask = NULL;
					taskid = gtid[taskcount];
					_single_mask(hw_sockets, 
						     hw_cores, 
						     hw_threads,
						     socket_index, 
						     core_index, 
						     thread_index, 
						     bind_to_exact_socket, 
						     bind_to_exact_core,
						     bind_to_exact_thread, 
						     &bitmask);
					xassert(masks[taskcount] == NULL);
					masks[taskcount] = bitmask;
					if (++taskcount >= maxtasks)
						goto fini;
				}
			}
		}
	}
 fini:	return SLURM_SUCCESS;
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
				   const uint32_t *gtid,
				   const uint32_t maxtasks,
				   bitstr_t ***masks_p)
{
	int retval, j, k, l, m, last_taskcount = -1, taskcount = 0, taskid = 0;
	int over_subscribe  = 0, space_remaining = 0;
	uint16_t core_index = 0, thread_index = 0;
	uint16_t hw_sockets = 0, hw_cores = 0, hw_threads = 0;
	uint16_t usable_cpus = 0, avail_cpus = 0;
	uint16_t usable_sockets = 0, usable_cores = 0, usable_threads = 0;

	bitstr_t **masks = NULL;
	bool bind_to_exact_socket = true;
	bool bind_to_exact_core   = true;
	bool bind_to_exact_thread = true;

	info("_task_layout_lllp_block ");

	retval = _task_layout_lllp_init(req, maxtasks, masks_p,
					&bind_to_exact_socket, 
					&bind_to_exact_core, 
					&bind_to_exact_thread,
					&usable_cpus, 
					&usable_sockets, 
					&usable_cores, 
					&usable_threads,
					&hw_sockets, 
					&hw_cores, 
					&hw_threads, 
					&avail_cpus);
	if (retval != SLURM_SUCCESS) {
		return retval;
	}
	masks = *masks_p;

	if (_init_lllp() != SLURM_SUCCESS) {
		error("In lllp_block: _init_lllp() != SLURM_SUCCESS");
		return SLURM_ERROR;
	}
	
	while(taskcount < maxtasks) {
		if (taskcount == last_taskcount) {
			error("_task_layout_lllp_block failure");
			return SLURM_ERROR;
		}
		last_taskcount = taskcount;
		for (j=0; j<usable_sockets; j++) {
			for(core_index=0; core_index < usable_cores; core_index++) {
				if((core_index < usable_cores) || (over_subscribe)) {
					for(thread_index=0; thread_index<usable_threads; thread_index++) {
						if((thread_index < usable_threads) || (over_subscribe)) {
							lllp_tasks->sockets[j].cores[core_index]
								.threads[thread_index].tasks++;
							taskcount++;
							if((thread_index+1) < usable_threads)
								space_remaining = 1;
							if(maxtasks <= taskcount) break;
						}
						if(maxtasks <= taskcount) break;
						if (!space_remaining) {
							over_subscribe = 1;
						} else {
							space_remaining = 0;
						}
					}
				}
				if(maxtasks <= taskcount) break;
				if((core_index+1) < usable_cores)
					space_remaining = 1;
				if (!space_remaining) {
					over_subscribe = 1;
				} else {
					space_remaining = 0;
				}
			}
			if(maxtasks <= taskcount) break;
			if (!space_remaining) {
				over_subscribe = 1;
			} else {
				space_remaining = 0;
			}
		}
	}
	
	/* Distribute the tasks and create masks for the task
	 * affinity plug-in */
	taskid = 0;
	taskcount = 0;
	for (j=0; j<usable_sockets; j++) {
		for (k=0; k<usable_cores; k++) {
			for (m=0; m<usable_threads; m++) {
				for (l=0; l<lllp_tasks->sockets[j]
					     .cores[k].threads[m].tasks; l++) {
					bitstr_t *bitmask = NULL;
					taskid = gtid[taskcount];
					_single_mask(hw_sockets, 
						     hw_cores, 
						     hw_threads,
						     j, k, m, 
						     bind_to_exact_socket, 
						     bind_to_exact_core, 
						     bind_to_exact_thread,
						     &bitmask);
					xassert(masks[taskcount] == NULL);
					xassert(taskcount < maxtasks);
					masks[taskcount] = bitmask;
					taskcount++;
				}
			}
		}
	}
	
	_print_tasks_per_lllp ();
	_cleanup_lllp();

	return SLURM_SUCCESS;
}

/* 
 * _task_layout_lllp_plane
 *
 * task_layout_lllp_plane will create a block cyclic distribution at
 * the lowest level of logical processor which is either socket, core or
 * thread depending on the system architecture. The Block algorithm is
 * different from the Block distribution performed at the node level
 * in that this algorithm does not load-balance the tasks across the
 * resources but uses the block size (i.e. plane size) specified by
 * the user.
 *
 *  Distribution at the lllp: 
 *  -m hostfile|plane|block|cyclic:block|cyclic 
 * 
 * The first distribution "hostfile|plane|block|cyclic" is computed
 * in srun. The second distribution "plane|block|cyclic" is computed
 * locally by each slurmd.
 *  
 * The input to the lllp distribution algorithms is the gids
 * (tasksids) generated for the local node.
 *  
 * The output is a mapping of the gids onto logical processors
 * (thread/core/socket)  with is expressed in cpu_bind masks.
 *
 */
static int _task_layout_lllp_plane(launch_tasks_request_msg_t *req, 
				   const uint32_t *gtid,
				   const uint32_t maxtasks,
				   bitstr_t ***masks_p)
{
	int retval, j, k, l, m, taskid = 0, last_taskcount = -1, next = 0;
	uint16_t core_index = 0, thread_index = 0;
	uint16_t hw_sockets = 0, hw_cores = 0, hw_threads = 0;
	uint16_t usable_cpus = 0, avail_cpus = 0;
	uint16_t usable_sockets = 0, usable_cores = 0, usable_threads = 0;
	uint16_t plane_size = req->plane_size;
	int max_plane_size = 0;

	bitstr_t **masks = NULL; 
	bool bind_to_exact_socket = true;
	bool bind_to_exact_core   = true;
	bool bind_to_exact_thread = true;

	info("_task_layout_lllp_plane %d ", req->plane_size);

	retval = _task_layout_lllp_init(req, maxtasks, masks_p,
					&bind_to_exact_socket, 
					&bind_to_exact_core, 
					&bind_to_exact_thread,
					&usable_cpus, 
					&usable_sockets, 
					&usable_cores, 
					&usable_threads,
					&hw_sockets, 
					&hw_cores, 
					&hw_threads, 
					&avail_cpus);
	if (retval != SLURM_SUCCESS) {
		return retval;
	}
	masks = *masks_p;

	max_plane_size = (plane_size > usable_cores) ? plane_size : usable_cores;
	next = 0;

	for (j=0; next<maxtasks; j++) {
		if (next == last_taskcount) {
			error("_task_layout_lllp_plan failure");
			return SLURM_ERROR;
		}
		last_taskcount = next;
		for (k=0; k<usable_sockets; k++) {
			max_plane_size = (plane_size > usable_cores) ? plane_size : usable_cores;
			for (m=0; m<max_plane_size; m++) {
				if(next>=maxtasks)
					break;
				core_index = m%usable_cores;				
				if(m<usable_cores) {
					for(l=0; l<usable_threads;l++) {
						if(next>=maxtasks)
							break;
						thread_index = l%usable_threads;
						
						if(thread_index<usable_threads) {
							bitstr_t *bitmask = NULL;
							taskid = gtid[next];
							_single_mask(hw_sockets,
								     hw_cores, 
								     hw_threads,
								     k, 
								     core_index, 
								     thread_index, 
								     bind_to_exact_socket, 
								     bind_to_exact_core,
								     bind_to_exact_thread, 
								     &bitmask);
							xassert(masks[next] == NULL);
							xassert(next < maxtasks);
							masks[next] = bitmask;
							next++;
						}
					}
				}
			}
		}
	}
	
	return SLURM_SUCCESS;
}

/*
 * slurm job state information
 * tracks jobids for which all future credentials have been revoked
 *  
 */
typedef struct {
	uint32_t jobid;
	uint32_t jobstepid;
	uint32_t numtasks;
	cpu_bind_type_t cpu_bind_type;
	char *cpu_bind;
} lllp_job_state_t;

static lllp_job_state_t *
_lllp_job_state_create(uint32_t job_id, uint32_t job_step_id,
		       cpu_bind_type_t cpu_bind_type, char *cpu_bind,
		       uint32_t numtasks)
{
	lllp_job_state_t *j;
	debug3("creating job [%u.%u] lllp state", job_id, job_step_id);

	j = xmalloc(sizeof(lllp_job_state_t));

	j->jobid	 = job_id;
	j->jobstepid	 = job_step_id;
	j->numtasks	 = numtasks;
	j->cpu_bind_type = cpu_bind_type;
	j->cpu_bind	 = NULL;
	if (cpu_bind) {
		j->cpu_bind = xmalloc(strlen(cpu_bind) + 1);
		strcpy(j->cpu_bind, cpu_bind);
	}
	return j;
}

static void
_lllp_job_state_destroy(lllp_job_state_t *j)
{
	debug3("destroying job [%u.%u] lllp state", j->jobid, j->jobstepid);
        if (j) {
		if (j->cpu_bind)
			xfree(j->cpu_bind);
	    	xfree(j);
	}
}

#if 0
/* Note: now inline in cr_release_lllp to support multiple job steps */
static lllp_job_state_t *
_find_lllp_job_state(uint32_t jobid)
{
        ListIterator  i = NULL;
        lllp_job_state_t  *j = NULL;

        i = list_iterator_create(lllp_ctx->job_list);
        while ((j = list_next(i)) && (j->jobid != jobid)) {;}
        list_iterator_destroy(i);
        return j;
}

static void
_remove_lllp_job_state(uint32_t jobid)
{
        ListIterator  i = NULL;
        lllp_job_state_t  *j = NULL;

        i = list_iterator_create(lllp_ctx->job_list);
        while ((j = list_next(i)) && (j->jobid != jobid)) {;}
	if (j) {
	    	list_delete_item(i);
	}
        list_iterator_destroy(i);
}
#endif

void
_append_lllp_job_state(lllp_job_state_t *j)
{
        list_append(lllp_ctx->job_list, j);
}

void
lllp_ctx_destroy(void)
{
	xfree(lllp_reserved);

    	if (lllp_ctx == NULL)
		return;

        xassert(lllp_ctx->magic == LLLP_CTX_MAGIC);

        slurm_mutex_lock(&lllp_ctx->mutex);
	list_destroy(lllp_ctx->job_list);

        xassert(lllp_ctx->magic = ~LLLP_CTX_MAGIC);

        slurm_mutex_unlock(&lllp_ctx->mutex);
        slurm_mutex_destroy(&lllp_ctx->mutex);

    	xfree(lllp_ctx);
}

void
lllp_ctx_alloc(void)
{
	uint32_t num_lllp;

	debug3("alloc LLLP");

	xfree(lllp_reserved);
	num_lllp = conf->sockets * conf->cores * conf->threads;
	if (conf->cpus > num_lllp) {
	    	num_lllp = conf->cpus;
	}
	lllp_reserved_size = num_lllp;
	lllp_reserved = xmalloc(num_lllp * sizeof(uint32_t));

	if (lllp_ctx) {
		lllp_ctx_destroy();
	}

        lllp_ctx = xmalloc(sizeof(*lllp_ctx));

        slurm_mutex_init(&lllp_ctx->mutex);
        slurm_mutex_lock(&lllp_ctx->mutex);
        
        lllp_ctx->job_list = NULL;
	lllp_ctx->job_list = list_create((ListDelF) _lllp_job_state_destroy);

        xassert(lllp_ctx->magic = LLLP_CTX_MAGIC);
        
        slurm_mutex_unlock(&lllp_ctx->mutex);
}

static int _init_lllp(void)
{
	int j = 0, k = 0;
	int usable_sockets, usable_threads, usable_cores;

	debug3("init LLLP");

  	/* FIX for heterogeneous socket/core/thread count per system
	 * in future releases */
	usable_sockets = conf->sockets;
	usable_threads = conf->threads;
	usable_cores   = conf->cores;

        lllp_tasks = xmalloc(sizeof(struct node_gids));
	lllp_tasks->sockets =  xmalloc(sizeof(struct socket_gids) * usable_sockets);
	for (j=0; j<usable_sockets; j++) {
		lllp_tasks->sockets[j].cores =  xmalloc(sizeof(struct core_gids) * usable_cores);
		for (k=0; k<usable_cores; k++) {
			lllp_tasks->sockets[j].cores[k].threads = 
					xmalloc(sizeof(struct thread_gids) * usable_threads);
		}
	}
	return SLURM_SUCCESS;
}

int _cleanup_lllp(void)
{
	int i=0, j=0;
  	/* FIX for heterogeneous socket/core/thread count per system in future releases */
	int usable_sockets = conf->sockets;
	int usable_cores   = conf->cores;

	for (i=0; i<usable_sockets; i++) { 
		for (j=0; j<usable_cores; j++) {
			xfree(lllp_tasks->sockets[i].cores[j].threads);
		}
		xfree(lllp_tasks->sockets[i].cores);
	}
	xfree(lllp_tasks->sockets);
	xfree(lllp_tasks);
	return SLURM_SUCCESS;
}

void _print_tasks_per_lllp (void)
{
	int j=0, k=0, l=0;
  	/* FIX for heterogeneous socket/core/thread count per system
	 * in future releases */
	int usable_sockets = conf->sockets;
	int usable_cores   = conf->cores;  
	int usable_threads = conf->threads;
  
	info("_print_tasks_per_lllp ");
  
	for(j=0; j < usable_sockets; j++) {
		for(k=0; k < usable_cores; k++) {
			for(l=0; l < usable_threads; l++) {
				info("socket %d core %d thread %d tasks %d ", j, k, l, 
				     lllp_tasks->sockets[j].cores[k].threads[l].tasks);
			}
		}
	}
}

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


/*
 * _single_mask
 *
 * This function allocates and returns a abstract (unmapped) bitmask given the
 * machine architecture, the index for the task, and the desired binding type
 */
static void _single_mask(const uint16_t nsockets, 
			 const uint16_t ncores, 
			 const uint16_t nthreads, 
			 const uint16_t socket_id,
			 const uint16_t core_id, 
			 const uint16_t thread_id,
			 const bool bind_to_exact_socket,
			 const bool bind_to_exact_core,
			 const bool bind_to_exact_thread,
			 bitstr_t **single_mask ) 
{
	int socket, core, thread;
	int nsockets_left, ncores_left, nthreads_left;
	bitoff_t bit;
	bitoff_t num_bits = nsockets * ncores * nthreads;
	bitstr_t * bitmask = bit_alloc(num_bits);

	if (bind_to_exact_socket) {
		nsockets_left = 1;
		socket = socket_id;
	} else {
		nsockets_left = nsockets;
		socket = 0;
	}	
	while (nsockets_left-- > 0) {
		if (bind_to_exact_core) {
			ncores_left = 1;
			core = core_id;
		} else {
			ncores_left = ncores;
			core = 0;
		}
		while (ncores_left-- > 0) {
			if (bind_to_exact_thread) { 
				nthreads_left = 1; 
				thread = thread_id;
			} else { 
				nthreads_left = nthreads; 
				thread = 0; 
			}
			while (nthreads_left-- > 0) {
				bit = SCT_TO_LLLP(socket, core, thread,
						  ncores, nthreads);
				if (bit < num_bits)
					bit_set(bitmask, bit);
				else
					info("Invalid job cpu_bind mask");
				thread++;
			}
			core++;
		}
		socket++;
	}
	
	*single_mask = bitmask;

#if(0)
	char *str = bit_fmt_hexmask(bitmask);
	info("_single_mask(Real: %d.%d.%d\t Use:%d.%d.%d\t = %s )",
	     nsockets, ncores, nthreads,
	     socket_id, core_id, thread_id, *str);
	xfree(str);
#endif
}


/*
 * cr_reserve_unit
 *
 * Given a bitstr_t, expand any set bits to cover the:
 * - entire socket if cr_type == CR_SOCKET or CR_SOCKET_MEMORY to 
 *   create a reservation for the entire socket.
 *     or
 * - entire core if cr_type == CR_CORE or CR_CORE_MEMORY to 
 *   create a reservation for the entire core.
 */
static void _cr_reserve_unit(bitstr_t *bitmask, int cr_type)
{
	uint32_t nsockets = conf->sockets;
	uint32_t ncores   = conf->cores;
	uint32_t nthreads = conf->threads;
	bitoff_t bit;
	int socket, core, thread;
	int nsockets_left, ncores_left, nthreads_left;
	int num_bits;
	bool reserve_this_socket = false;
	bool reserve_this_core   = false;

	if (!bitmask) {
	    	return;
	}
	if ((cr_type != CR_SOCKET) &&
	    (cr_type != CR_SOCKET_MEMORY) &&
	    (cr_type != CR_CORE) &&
	    (cr_type != CR_CORE_MEMORY)) {
		return;
	}

	num_bits = bit_size(bitmask);
	nsockets_left = nsockets;
	socket = 0;
	while (nsockets_left-- > 0) {
		reserve_this_socket = false;
		ncores_left = ncores;
		core = 0;
		while (ncores_left-- > 0) { /* check socket for set bits */
			reserve_this_core = false;
			nthreads_left = nthreads; 
			thread = 0; 
			while (nthreads_left-- > 0) {
				bit = SCT_TO_LLLP(socket, core, thread,
						  ncores, nthreads);
				/* map abstract to machine */
				bit = BLOCK_MAP(bit);
				if (bit < num_bits) {
					if (bit_test(bitmask,bit)) {
						reserve_this_socket = true;
						reserve_this_core   = true;
						nthreads_left = 0;
					}
				} else
					info("Invalid job cpu_bind mask");
				thread++;
			}
			/* mark entire core */
			if (((cr_type == CR_CORE) ||
			     (cr_type == CR_CORE_MEMORY)) &&
			    reserve_this_core) {
				nthreads_left = nthreads; 
				thread = 0; 
				while (nthreads_left-- > 0) {
					bit = SCT_TO_LLLP(socket, core, thread,
							  ncores, nthreads);
					/* map abstract to machine */
					bit = BLOCK_MAP(bit);
					if (bit < num_bits)
						bit_set(bitmask, bit);
					else
						info("Invalid job cpu_bind mask");
					thread++;
				}
			}
			core++;
		}
		/* mark entire socket */
		if (((cr_type == CR_SOCKET) ||
		     (cr_type == CR_SOCKET_MEMORY)) &&
		    reserve_this_socket) {
			ncores_left = ncores;
			core = 0;
			while (ncores_left-- > 0) {
				nthreads_left = nthreads; 
				thread = 0; 
				while (nthreads_left-- > 0) {
					bit = SCT_TO_LLLP(socket, core, thread,
							  ncores, nthreads);
					/* map abstract to machine */
					bit = BLOCK_MAP(bit);
					if (bit < num_bits)
						bit_set(bitmask, bit);
					else
						info("Invalid job cpu_bind mask");
					thread++;
				}
				core++;
			}
		}
		socket++;
	}
	
}


static int _get_bitmap_from_cpu_bind(bitstr_t *bitmap_test,
				     cpu_bind_type_t cpu_bind_type, 
				     char *cpu_bind, uint32_t numtasks)
{
	char opt_dist[10];
	char *dist_str = NULL;
	char *dist_str_next = NULL;
	int bitmap_size = bit_size(bitmap_test);
	int rc = SLURM_SUCCESS;
	unsigned int i;
	dist_str = cpu_bind;
	
	if (cpu_bind_type & CPU_BIND_RANK) {
		for (i=0; i<numtasks; i++) {
			if (i < bitmap_size)
				bit_set(bitmap_test, i);
			else {
				info("Invalid job cpu_bind mask");
				return SLURM_ERROR;
			}
		}
		return rc;
	}

	i = 0;
	while (dist_str != NULL) {
		if (i >= numtasks) {	/* no more tasks need masks */
		    	break;
		}
		if (*dist_str == ',') {	/* get next mask from cpu_bind */
			dist_str++;
		}
		dist_str_next = strchr(dist_str, ',');

		if (dist_str_next != NULL) {
			strncpy(opt_dist, dist_str, dist_str_next-dist_str);
			opt_dist[dist_str_next-dist_str] = '\0';
		} else {
			strcpy(opt_dist, dist_str);
		}

		/* add opt_dist to bitmap_test */
		if (cpu_bind_type & CPU_BIND_MASK) {
			bit_unfmt_hexmask(bitmap_test, opt_dist);
		} else if (cpu_bind_type & CPU_BIND_MAP) {
			unsigned int mycpu = 0;
			if (strncmp(opt_dist, "0x", 2) == 0) {
				mycpu = strtoul(&(opt_dist[2]), NULL, 16);
			} else {
				mycpu = strtoul(opt_dist, NULL, 10);
			}
			if (mycpu < bitmap_size)
				bit_set(bitmap_test, mycpu);
			else {
				info("Invalid job cpu_bind mask");
				rc = SLURM_ERROR;
				/* continue and try to map remaining tasks */
			}
		}

		dist_str = dist_str_next;
		dist_str_next = NULL;
	    	i++;
	}
	return rc;
}


static void _cr_update_reservation(int reserve, uint32_t *reserved, 
				   bitstr_t *mask)
{
	int i;
	int num_bits = bit_size(mask);

	for(i=0; i < num_bits; i++) {
		if (bit_test(mask,i)) {
			if (reserve) {
				/* reserve LLLP */
				reserved[i]++;
			} else {
				/* release LLLP only if non-zero */
				if (reserved[i] > 0) {
					reserved[i]--;
				}
			}
		}
	}
}

static void _cr_update_lllp(int reserve, uint32_t job_id, uint32_t job_step_id,
			    cpu_bind_type_t cpu_bind_type, char *cpu_bind,
			    uint32_t numtasks)
{
	int buf_len = 1024;
	char buffer[buf_len], buftmp[128], buf_action[20];	/* for info */

	if (lllp_reserved == NULL) {
	    	/* fixme: lllp_reserved not allocated */
	    	return;
	}

	if ((cpu_bind_type & CPU_BIND_RANK) ||
	    (cpu_bind_type & CPU_BIND_MASK) ||
	    (cpu_bind_type & CPU_BIND_MAP)) {
		int i = 0;
		bitoff_t num_bits = 
			conf->sockets * conf->cores * conf->threads;
		bitstr_t * bitmap_test = bit_alloc(num_bits);
		_get_bitmap_from_cpu_bind(bitmap_test,
					  cpu_bind_type, cpu_bind, numtasks);

		_cr_reserve_unit(bitmap_test, conf->cr_type);

		_cr_update_reservation(reserve, lllp_reserved, bitmap_test);

		bit_free(bitmap_test);	/* not currently stored with job_id */

		/*** display the updated lllp_reserved counts ***/
		buffer[0] = '\0';
		for (i=num_bits-1; i >=0; i--) {
			sprintf(buftmp, "%d", lllp_reserved[i]);
			if (strlen(buftmp) + strlen(buffer) + 1 < buf_len) {
			        if (i < (num_bits-1)) strcat(buffer,",");
				strcat(buffer,buftmp);
			} else {/* out of space...indicate incomplete string */
				buffer[strlen(buffer)-1] = '*';
				buffer[strlen(buffer)] = '\0';
				break;
			}
		}
		if (reserve) {
			strcpy(buf_action, "reserve");
		} else {
			strcpy(buf_action, "release");
		}
		info("LLLP update %s [%u.%u]: %s (CPU IDs: %d...0)",
			buf_action, job_id, job_step_id, buffer, num_bits-1);
	}
}


void cr_reserve_lllp(uint32_t job_id,
			launch_tasks_request_msg_t *req, uint32_t node_id)
{
	lllp_job_state_t *j;
	cpu_bind_type_t cpu_bind_type = req->cpu_bind_type;
	char *cpu_bind = req->cpu_bind;
	uint32_t numtasks = 0;
	char buf_type[100];

	debug3("reserve LLLP job [%u.%u]\n", job_id, req->job_step_id);

	if (req->tasks_to_launch) {
		numtasks = req->tasks_to_launch[(int)node_id];
	}

	slurm_sprint_cpu_bind_type(buf_type, cpu_bind_type);
	debug3("reserve lllp job [%u.%u]: %d tasks; %s[%d], %s",
	       job_id, req->job_step_id, numtasks,
	       buf_type, cpu_bind_type, cpu_bind);
	if (cpu_bind_type == 0)
		return;


    	/* store job_id, cpu_bind_type, cpu_bind */
	slurm_mutex_lock(&lllp_ctx->mutex);

	j = _lllp_job_state_create(job_id, req->job_step_id,
					cpu_bind_type, cpu_bind, numtasks);

	if (j) {
		_append_lllp_job_state(j);
		_cr_update_lllp(1, job_id, req->job_step_id,
				cpu_bind_type, cpu_bind, numtasks);
	}
	slurm_mutex_unlock(&lllp_ctx->mutex);
}

void cr_release_lllp(uint32_t job_id)
{
	ListIterator  i = NULL;
	lllp_job_state_t *j;
	cpu_bind_type_t cpu_bind_type = 0;
	char *cpu_bind = NULL;
	uint32_t numtasks = 0;
	char buf_type[100];

	debug3("release LLLP job [%u.*]", job_id);

    	/* retrieve cpu_bind_type, cpu_bind from job_id */
	slurm_mutex_lock(&lllp_ctx->mutex);
	i = list_iterator_create(lllp_ctx->job_list);
	while ((j = list_next(i))) {
		if (j->jobid == job_id) {
			cpu_bind_type = j->cpu_bind_type;
			cpu_bind      = j->cpu_bind;
			numtasks      = j->numtasks;
			slurm_sprint_cpu_bind_type(buf_type, cpu_bind_type);
			debug3("release search lllp job %u: %d tasks; %s[%d], %s",
			       j->jobid, numtasks,
			       buf_type, cpu_bind_type, cpu_bind);

			_cr_update_lllp(0, job_id, j->jobstepid,
					cpu_bind_type, cpu_bind, numtasks);

			/* done with saved state, remove entry */
			list_delete_item(i);
		}
	}
	list_iterator_destroy(i);
	slurm_mutex_unlock(&lllp_ctx->mutex);
}


