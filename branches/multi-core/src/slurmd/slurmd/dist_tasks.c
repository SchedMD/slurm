/*****************************************************************************\
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include "src/slurmd/slurmd/dist_tasks.h"

slurm_lllp_ctx_t *lllp_ctx;	/* binding context */
struct node_gids *lllp_tasks; /* Keep track of the task count for
			       * logical processors
			       * socket/core/thread.
			       */

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
static void _single_mask(const int nsockets, 
			 const int ncores, 
			 const int nthreads, 
			 const int socket_id,
			 const int core_id, 
			 const int thread_id,
			 const bool bind_to_exact_socket,
			 const bool bind_to_exact_core,
			 const bool bind_to_exact_thread,
			 bitstr_t ** single_mask);
static void _get_resources_this_node(int *cpus,
				     int *sockets,
				     int *cores,
				     int *threads,
				     int *alloc_sockets, 
				     int *alloc_lps);
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

static uint32_t _block_map(uint32_t index, uint32_t *map);

/* 
 * lllp_distribution
 *
 * Note: lllp stands for lowest level of logical processors. 
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
void lllp_distribution(launch_tasks_request_msg_t *req, 
		       const uint32_t *gtid)
{
	int rc = SLURM_SUCCESS;
	bitstr_t **masks = NULL;
	char buf_type[100];
	int maxtasks = req->tasks_to_launch[req->srun_node_id];
	
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

	info("lllp_distribution jobid [%u] auto binding: %s, dist %d",
	     req->job_id, buf_type, req->task_dist);

	switch (req->task_dist) {
	case SLURM_DIST_BLOCK_BLOCK:
	case SLURM_DIST_CYCLIC_BLOCK:
		_task_layout_lllp_block(req, gtid, maxtasks, &masks);
		break;
	case SLURM_DIST_CYCLIC:
	case SLURM_DIST_BLOCK:
	case SLURM_DIST_CYCLIC_CYCLIC:
	case SLURM_DIST_BLOCK_CYCLIC:
		_task_layout_lllp_cyclic(req, gtid, maxtasks, &masks); 
		break;
	case SLURM_DIST_PLANE:
		_task_layout_lllp_plane(req, gtid, maxtasks, &masks); 
		break;
	default:
		_task_layout_lllp_cyclic(req, gtid, maxtasks, &masks); 
		req->task_dist = SLURM_DIST_BLOCK_CYCLIC;
		break;
	}

	if (masks) {
		_task_layout_display_masks(req, gtid, maxtasks, masks); 
	    	_lllp_use_available(req, maxtasks, masks);
		_task_layout_display_masks(req, gtid, maxtasks, masks); 
	    	_lllp_map_abstract_masks(maxtasks, masks);
		_task_layout_display_masks(req, gtid, maxtasks, masks); 
	    	_lllp_generate_cpu_bind(req, maxtasks, masks);
	    	_lllp_free_masks(req, maxtasks, masks);
	}

	if(rc != SLURM_SUCCESS)
		error (" Error in lllp_distribution_create %s ", 
		       req->task_dist);
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
	int resv_incr, i, j;
	uint32_t *resv;
	int rotval, prev_rotval, rot_cnt;

	if (conf->cr_type == SELECT_TYPE_INFO_NONE) {
	    	return;	/* not using CR, don't need to check availability */
	}

	/* select the unit of reservation rotation increment based on CR type */
	if (conf->cr_type == CR_SOCKET) {
		resv_incr = conf->cores * conf->threads;
	} else if (conf->cr_type == CR_CORE) {
		resv_incr = conf->threads;
	} else {
		resv_incr = 1;
	}

	debug3("_lllp_use_available resv_incr = %d", resv_incr);

	/* get a copy of the current reservations */
	resv = xmalloc(conf->lllp_reserved_size * sizeof(uint32_t));
        memcpy(resv, conf->lllp_reserved, conf->lllp_reserved_size * sizeof(uint32_t));

	/* check each mask against current reservations */
	rotval      = 0;
	prev_rotval = 0;
	for (i = 0; i < maxtasks; i++) {
		bitstr_t *bitmask = masks[i];
		bitstr_t *physmask = NULL;
		int min_overlap = INT_MAX;
		int min_rotval  = 0;

		/* create masks that are at least as large as the reservation */
		int bitmask_size = bit_size(bitmask);
		int newmask_size = MAX(bitmask_size, conf->lllp_reserved_size);

		/* get maximum number of contiguous bits in bitmask */
		int contig_bits = bit_nset_max_count(bitmask);

		/* make sure the reservation increment is larger than the number
		 * of contiguous bits in the mask to maintain any properties
		 * present in the mask (e.g. use both cores on one socket
		 */
		int this_resv_incr = resv_incr;
		while(resv_incr < contig_bits) {
			resv_incr *= 2;
		}

		/* rotate mask to find the minimum reservation overlap starting
		 * with the previous rotation value
		 */
		rotval  = prev_rotval;
		rot_cnt = newmask_size / resv_incr;
		debug3("mask %d contig:%d incr:%d rot_cnt:%d",
		       i, contig_bits, resv_incr, rot_cnt);
		for (j = 0; j < rot_cnt; j++) {
			int overlap;		       
			bitstr_t *newmask = bit_rotate_copy(bitmask, rotval,
							    newmask_size);
			physmask = _lllp_map_abstract_mask(newmask);
			overlap = int_and_set_count((int *)resv,
						    conf->lllp_reserved_size,
						    physmask);
			bit_free(newmask);
			bit_free(physmask);
			debug3("mask %d rot %d[%d] = %d",
			       i, rotval, j, overlap);
			if (overlap < min_overlap) {
				min_overlap = overlap;
				min_rotval  = rotval;
			}
			if (overlap == 0) {	/* no overlap, stop rotating */
				break;
			}
			rotval += this_resv_incr;
		}
		rotval = min_rotval;
		if (rotval != 0) {
			bitstr_t *newmask = bit_rotate_copy(bitmask, rotval,
							    newmask_size);
			bit_free(masks[i]);
			masks[i] = newmask;
		}

		debug3("mask %d using rot %d", i, rotval);
		/* accepted current mask, add to copy of the reservations */
		physmask = _lllp_map_abstract_mask(masks[i]);
		_cr_update_reservation(1, resv, physmask);
		bit_free(physmask);
		prev_rotval = rotval;
	}
	/* xfree(resv); */
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

	bit_nclear(newmask,0,num_bits-1); /* init to zero */
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
	if(masks_str == NULL) {
		error(" JobId %u masks_str == NULL", req->job_id); 
		return;
	}

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
 * _task_layout_lllp_init
 *
 * task_layout_lllp_init performs common initialization required by:
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
				  int *usable_cpus,
				  int *usable_sockets, 
				  int *usable_cores,
				  int *usable_threads,
				  int *hw_sockets, 
				  int *hw_cores,
				  int *hw_threads,
				  int *alloc_sockets,
				  int *alloc_lps,
				  int *avail_cpus)
{
	int i;

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

	*alloc_sockets = 0;
	*alloc_lps     = 0;
	_get_resources_this_node(usable_cpus, usable_sockets, usable_cores,
				 usable_threads, alloc_sockets, alloc_lps);

	*hw_sockets = *usable_sockets;
	*hw_cores   = *usable_cores;
	*hw_threads = *usable_threads;

	*avail_cpus = slurm_get_avail_procs(req->max_sockets, req->max_cores, 
					    req->max_threads, req->cpus_per_task,
					    usable_cpus, usable_sockets,
					    usable_cores, usable_threads,
					    *alloc_sockets, *alloc_lps, conf->cr_type);
	
	/* Allocate masks array */
	*masks_p = xmalloc(maxtasks * sizeof(bitstr_t*));
	if(*masks_p == NULL) {
		error(" JobId %u masks == NULL", req->job_id); 
		return SLURM_ERROR;
	}
	for (i = 0; i < maxtasks; i++) { 
	    	(*masks_p)[i] = NULL;
	}
	return SLURM_SUCCESS;
}

/* _get_resources_this_node determines counts for already allocated
 * resources (currently sockets and lps) for this node.  
 *
 * Only used when cons_res (Consumable Resources) is enabled with
 * CR_Socket, CR_Cores, or CR_Default.
 *
 * OUT- Number of allocated sockets on this node
 * OUT- Number of allocated logical processors on this node 
 * 
 */
static void _get_resources_this_node(int *cpus,
				     int *sockets,
				     int *cores,
				     int *threads,
				     int *alloc_sockets, 
				     int *alloc_lps)
{
	int bit_index = 0;
	int i, j , k;
	int this_socket = 0;

	/* FIX for heterogeneous socket/core/thread count per system in future releases */
	*cpus    = conf->cpus;
	*sockets = conf->sockets;
	*cores   = conf->cores;
	*threads = conf->threads;

	if ((conf->cr_type == CR_SOCKET) || (conf->cr_type == CR_DEFAULT)) {	
		for(i = 0; i < *sockets; i++) {
			this_socket = 0;
			for(j = 0; j < *cores; j++) {
				for(k = 0; k < *threads; k++) {
					if(conf->lllp_reserved[bit_index] > 0) {
						*alloc_lps += 1;
						this_socket++;
					}
					bit_index++;
				}
			}
			if (this_socket > 0) {
				*alloc_sockets += 1;
			}
		}
		
		xassert(bit_index == (*sockets * *cores * *threads));
	}

	info("_get_resources hostname %s alloc_sockets %d alloc_lps %d ", 
	     conf->hostname, *alloc_sockets, *alloc_lps);
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
	int retval, i, taskcount = 0, taskid = 0;
	int over_subscribe  = 0, space_remaining = 0;
	int socket_index = 0, core_index = 0, thread_index = 0;
	int hw_sockets = 0, hw_cores = 0, hw_threads = 0;
	int usable_cpus = 0, avail_cpus = 0;
	int usable_sockets = 0, usable_cores = 0, usable_threads = 0;
	int alloc_sockets = 0, alloc_lps = 0;
	
	bitstr_t **masks = NULL;
	bool bind_to_exact_socket = true;
	bool bind_to_exact_core   = true;
	bool bind_to_exact_thread = true;
	
	info ("_task_layout_lllp_cyclic ");

	retval = _task_layout_lllp_init(req, maxtasks, masks_p,
					&bind_to_exact_socket, &bind_to_exact_core, &bind_to_exact_thread,
					&usable_cpus, &usable_sockets, &usable_cores, &usable_threads,
					&hw_sockets, &hw_cores, &hw_threads, &alloc_sockets, &alloc_lps,
					&avail_cpus);
	if (retval != SLURM_SUCCESS) {
		return retval;
	}
	masks = *masks_p;

	for (i=0; taskcount<maxtasks; i++) { 
		space_remaining = 0;
		socket_index = 0;
		for (thread_index=0; ((thread_index<usable_threads)
				      && (taskcount<maxtasks)); thread_index++) {
			for (core_index=0; ((core_index<usable_cores)
					    && (taskcount<maxtasks)); core_index++) {
				for (socket_index=0; ((socket_index<usable_sockets)
						      && (taskcount<maxtasks)); socket_index++) {
					if ((socket_index<usable_sockets) || over_subscribe) {
						if ((core_index<usable_cores) || over_subscribe) {
							if ((thread_index<usable_threads) || over_subscribe) {
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
								xassert(taskcount < maxtasks);
								masks[taskcount] = bitmask;
								taskcount++;
								if ((thread_index+1) < usable_threads)
									space_remaining = 1;
							}
							if ((core_index+1) < usable_cores)
								space_remaining = 1;
						}
					}
				}
				if (!space_remaining)
					over_subscribe = 1;
			}
		}
	}

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
				   const uint32_t *gtid,
				   const uint32_t maxtasks,
				   bitstr_t ***masks_p)
{
        int retval, j, k, l, m, taskcount = 0, taskid = 0;
	int over_subscribe  = 0, space_remaining = 0;
	int core_index = 0, thread_index = 0;
	int hw_sockets = 0, hw_cores = 0, hw_threads = 0;
	int usable_cpus = 0, avail_cpus = 0;
	int usable_sockets = 0, usable_cores = 0, usable_threads = 0;
	int alloc_sockets = 0, alloc_lps = 0;

	bitstr_t **masks = NULL;
	bool bind_to_exact_socket = true;
	bool bind_to_exact_core   = true;
	bool bind_to_exact_thread = true;

	info("_task_layout_lllp_block ");

	retval = _task_layout_lllp_init(req, maxtasks, masks_p,
					&bind_to_exact_socket, &bind_to_exact_core, &bind_to_exact_thread,
					&usable_cpus, &usable_sockets, &usable_cores, &usable_threads,
					&hw_sockets, &hw_cores, &hw_threads, &alloc_sockets, &alloc_lps,
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
	   affinity plug-in */
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
 * Restriction: Any restrictions? what about plane:cyclic or
 * plane:block? Only plane:plane? FIXME!!! SMB!!!
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
        int retval, j, k, l, m, taskid = 0, next = 0;
	int core_index = 0, thread_index = 0;
	int hw_sockets = 0, hw_cores = 0, hw_threads = 0;
	int usable_cpus = 0, avail_cpus = 0;
	int usable_sockets = 0, usable_cores = 0, usable_threads = 0;
	int plane_size = req->plane_size;
	int max_plane_size = 0;
	int alloc_sockets = 0, alloc_lps = 0;

	bitstr_t **masks = NULL; 
	bool bind_to_exact_socket = true;
	bool bind_to_exact_core   = true;
	bool bind_to_exact_thread = true;

	info("_task_layout_lllp_plane %d ", req->plane_size);

	retval = _task_layout_lllp_init(req, maxtasks, masks_p,
					&bind_to_exact_socket, &bind_to_exact_core, &bind_to_exact_thread,
					&usable_cpus, &usable_sockets, &usable_cores, &usable_threads,
					&hw_sockets, &hw_cores, &hw_threads, &alloc_sockets, &alloc_lps,
					&avail_cpus);
	if (retval != SLURM_SUCCESS) {
		return retval;
	}
	masks = *masks_p;

	max_plane_size = (plane_size > usable_cores) ? plane_size : usable_cores;
	next = 0;

	for (j=0; next<maxtasks; j++) {
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
	uint32_t numtasks;
        cpu_bind_type_t cpu_bind_type;
        char *cpu_bind;
} lllp_job_state_t;

static lllp_job_state_t *
_lllp_job_state_create(uint32_t job_id,
		       cpu_bind_type_t cpu_bind_type, char *cpu_bind,
		       uint32_t numtasks)
{
	lllp_job_state_t *j;
        debug3("creating job %d lllp state", job_id);

	j = xmalloc(sizeof(lllp_job_state_t));

	j->jobid	 = job_id;
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
        debug3("destroying job %d lllp state", j->jobid);
        if (j) {
		if (j->cpu_bind)
			xfree(j->cpu_bind);
	    	xfree(j);
	}
}
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
	    	list_delete(i);
	}
        list_iterator_destroy(i);
}

void
_insert_lllp_job_state(lllp_job_state_t *j)
{
        list_append(lllp_ctx->job_list, j);
}

void
lllp_ctx_destroy(void)
{
	if (conf->lllp_reserved) {
		xfree(conf->lllp_reserved);
	}

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

	if (conf->lllp_reserved) {
		xfree(conf->lllp_reserved);
	}
	num_lllp = conf->sockets * conf->cores * conf->threads;
	conf->lllp_reserved_size = num_lllp;
	conf->lllp_reserved = xmalloc(num_lllp * sizeof(uint32_t));
	memset(conf->lllp_reserved, 0, num_lllp * sizeof(uint32_t));
	if (conf->lllp_reserved == NULL)
		error(" conf->lllp_reserved == NULL");

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

  	/* FIX for heterogeneous socket/core/thread count per system in future releases */
	usable_sockets = conf->sockets;
	usable_threads = conf->threads;
	usable_cores   = conf->cores;

        lllp_tasks = xmalloc(sizeof(struct node_gids));
	if (lllp_tasks == NULL) {
		error(" Error (lllp_tasks == NULL) ");
		return SLURM_ERROR;
	}
	
	lllp_tasks->sockets =  xmalloc(sizeof(struct socket_gids) * usable_sockets);
	if (lllp_tasks->sockets == NULL) {
		error(" Error (lllp_tasks->sockets == NULL) ");
		return SLURM_ERROR;
	}
	for (j=0; j<usable_sockets; j++) {
		lllp_tasks->sockets[j].cores =  xmalloc(sizeof(struct core_gids) * usable_cores);
		if (lllp_tasks->sockets[j].cores == NULL) {
			error(" Error (lllp_tasks->sockets[j].cores == NULL) ");
			return SLURM_ERROR;
		}
		for (k=0; k<usable_cores; k++) {
			lllp_tasks->sockets[j].cores[k].threads = xmalloc(sizeof(struct thread_gids) * usable_threads);
			if (lllp_tasks->sockets[j].cores[k].threads == NULL) {
				error(" Error (lllp_tasks->sockets[j].cores[k].threads == NULL) ");
				return SLURM_ERROR;
			}
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
	xfree(lllp_tasks->sockets);;
	xfree(lllp_tasks);
	return SLURM_SUCCESS;
}

void _print_tasks_per_lllp (void)
{
	int j=0, k=0, l=0;
  	/* FIX for heterogeneous socket/core/thread count per system in future releases */
	int usable_sockets = conf->sockets; /* FIXME */
	int usable_cores   = conf->cores;   /* FIXME */
	int usable_threads = conf->threads; /* FIXME */
  
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
static uint32_t _block_map(uint32_t index, uint32_t *map)
{
	if (map == NULL) {
	    	return index;
	}
	/* make sure bit falls in map */
	if (index >= conf->block_map_size) {
		debug3("wrapping index %d into block_map_size of %d",
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
static void _single_mask(const int nsockets, 
			 const int ncores, 
			 const int nthreads, 
			 const int socket_id,
			 const int core_id, 
			 const int thread_id,
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
	bit_nclear(bitmask,0,num_bits-1); /* init to zero */

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
				bit_set(bitmask, bit);
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
 * - entire socket if cr_type == CR_SOCKET to create a reservation 
 *   for the entire socket.
 *     or
 * - entire core if cr_type == CR_CORE to create a reservation 
 *   for the entire core.
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
	    (cr_type != CR_CORE)) {
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
				}
				thread++;
			}
			/* mark entire core */
			if ((cr_type == CR_CORE) && reserve_this_core) {
				nthreads_left = nthreads; 
				thread = 0; 
				while (nthreads_left-- > 0) {
					bit = SCT_TO_LLLP(socket, core, thread,
							  ncores, nthreads);
					/* map abstract to machine */
					bit = BLOCK_MAP(bit);
					bit_set(bitmask, bit);
					thread++;
				}
			}
			core++;
		}
		/* mark entire socket */
		if ((cr_type == CR_SOCKET) && reserve_this_socket) {
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
					bit_set(bitmask, bit);
					thread++;
				}
				core++;
			}
		}
		socket++;
	}
	
}


void get_bitmap_from_cpu_bind(bitstr_t *bitmap_test,
			      cpu_bind_type_t cpu_bind_type, 
			      char *cpu_bind, uint32_t numtasks)
{
	char opt_dist[10];
	char *dist_str = NULL;
	char *dist_str_next = NULL;
	dist_str = cpu_bind;
	
	if (cpu_bind_type & CPU_BIND_RANK) {
		unsigned int i = 0;
		for (i = 0; i < MIN(numtasks,bit_size(bitmap_test)); i++) {
			bit_set(bitmap_test, i);
		}
		return;
	}

	while (dist_str != NULL) {
	    	/* get next mask from cpu_bind */
		if (*dist_str == ',') {
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
			bit_set(bitmap_test, mycpu);
		}

		dist_str = dist_str_next;
		dist_str_next = NULL;
	}
}


static void _cr_update_reservation(int reserve, uint32_t *reserved, 
				   bitstr_t *mask)
{
	int i;
	int num_bits = bit_size(mask);

	for(i=0; i < num_bits; i++) {
		if (bit_test(mask,i))
		{
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

static void _cr_update_lllp(int reserve,
			    cpu_bind_type_t cpu_bind_type, char *cpu_bind,
			    uint32_t numtasks)
{
	int buf_len = 1024;
	char buffer[buf_len], buftmp[128], buf_action[20];	/* for info */

	if (conf->lllp_reserved == NULL) {
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
		bit_nclear(bitmap_test,0,num_bits-1); /* init to zero */
		get_bitmap_from_cpu_bind(bitmap_test,
					 cpu_bind_type, cpu_bind, numtasks);

		_cr_reserve_unit(bitmap_test, conf->cr_type);

		_cr_update_reservation(reserve, conf->lllp_reserved, 
				       bitmap_test);

		bit_free(bitmap_test);	/*** fixme: store with job_id? ***/

		/*** display the updated lllp_reserved counts ***/
		buffer[0] = '\0';
		for (i=0; i < num_bits; i++) {
			sprintf(buftmp, "%d", conf->lllp_reserved[i]);
			if (strlen(buftmp) + strlen(buffer) + 1 < buf_len) {
			        if (i) strcat(buffer,",");
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
		info("LLLP update %s %s ", buf_action, buffer);
	}
}


void cr_reserve_lllp(uint32_t job_id, launch_tasks_request_msg_t *req)
{
	lllp_job_state_t *j;
	cpu_bind_type_t cpu_bind_type = req->cpu_bind_type;
	char *cpu_bind = req->cpu_bind;
	uint32_t srun_node_id = req->srun_node_id;
	uint32_t numtasks = 0;
	char buf_type[100];

	debug3("reserve LLLP %d\n", job_id);

	if (req->tasks_to_launch) {
		numtasks = req->tasks_to_launch[srun_node_id];
	}

	slurm_sprint_cpu_bind_type(buf_type, cpu_bind_type);
	debug3("reserve lllp job %d: %d tasks; %s[%d], %s",
	       job_id, numtasks,
	       buf_type, cpu_bind_type, cpu_bind);
	if (cpu_bind_type == 0)
		return;


    	/* store job_id, cpu_bind_type, cpu_bind */
	slurm_mutex_lock(&lllp_ctx->mutex);

	if ((j = _find_lllp_job_state(job_id))) {
		_remove_lllp_job_state(job_id);	/* clear any stale state */
	}

	j = _lllp_job_state_create(job_id, cpu_bind_type, cpu_bind, numtasks);

	if (j) {
		_insert_lllp_job_state(j);
		_cr_update_lllp(1, cpu_bind_type, cpu_bind, numtasks);
	}
	slurm_mutex_unlock(&lllp_ctx->mutex);
}

void cr_release_lllp(uint32_t job_id)
{
	lllp_job_state_t *j;
	cpu_bind_type_t cpu_bind_type = 0;
	char *cpu_bind = NULL;
	uint32_t numtasks = 0;
	char buf_type[100];

	debug3("release LLLP %d", job_id);

    	/* retrieve cpu_bind_type, cpu_bind from job_id */
	slurm_mutex_lock(&lllp_ctx->mutex);
	j = _find_lllp_job_state(job_id);
	if (j) {
	    	cpu_bind_type = j->cpu_bind_type;
		cpu_bind      = j->cpu_bind;
		numtasks      = j->numtasks;
		slurm_sprint_cpu_bind_type(buf_type, cpu_bind_type);
		debug3("release search lllp job %d: %d tasks; %s[%d], %s",
		       j->jobid, numtasks,
		       buf_type, cpu_bind_type, cpu_bind);

		_cr_update_lllp(0, cpu_bind_type, cpu_bind, numtasks);

		/* done with saved state, remove entry */
		_remove_lllp_job_state(job_id);
	}
	slurm_mutex_unlock(&lllp_ctx->mutex);
}


