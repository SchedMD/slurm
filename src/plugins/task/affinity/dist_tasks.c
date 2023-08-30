/*****************************************************************************\
 *  Copyright (C) 2006-2009 Hewlett-Packard Development Company, L.P.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
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

#define _GNU_SOURCE

#include "affinity.h"
#include "dist_tasks.h"
#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/interfaces/cred.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/strlcpy.h"
#include "src/common/xmalloc.h"
#include "src/slurmd/slurmd/slurmd.h"

#ifdef HAVE_NUMA
#include <numa.h>
#endif

static char *_alloc_mask(launch_tasks_request_msg_t *req,
			 int *whole_node_cnt, int *whole_socket_cnt,
			 int *whole_core_cnt, int *whole_thread_cnt,
			 int *part_socket_cnt, int *part_core_cnt);
static bitstr_t *_get_avail_map(slurm_cred_t *cred, uint16_t *hw_sockets,
				uint16_t *hw_cores, uint16_t *hw_threads);
static int _get_local_node_info(slurm_cred_arg_t *arg, int job_node_id,
				uint16_t *sockets, uint16_t *cores);

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
		       req->step_id.job_id, gtid[i], str);
		xfree(str);
	}
}

static void _lllp_free_masks(const uint32_t maxtasks, bitstr_t **masks)
{
    	int i;
	bitstr_t *bitmask;

	for (i = 0; i < maxtasks; i++) {
		bitmask = masks[i];
		FREE_NULL_BITMAP(bitmask);
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
	bitstr_t *hw_map;
	int task_cnt = 0;

#ifdef HAVE_FRONT_END
{
	/* Since the front-end nodes are a shared resource, we limit each job
	 * to one CPU based upon monotonically increasing sequence number */
	static int last_id = 0;
	hw_map  = (bitstr_t *) bit_alloc(conf->block_map_size);
	bit_set(hw_map, ((last_id++) % conf->block_map_size));
	task_cnt = 1;
}
#else
{
	uint16_t sockets = 0, cores = 0, threads = 0;
	hw_map = _get_avail_map(req->cred, &sockets, &cores, &threads);
	if (hw_map)
		task_cnt = bit_set_count(hw_map);
}
#endif
	if (task_cnt) {
		req->cpu_bind_type = CPU_BIND_MASK;
		if (slurm_conf.task_plugin_param & CPU_BIND_VERBOSE)
			req->cpu_bind_type |= CPU_BIND_VERBOSE;
		xfree(req->cpu_bind);
		req->cpu_bind = (char *)bit_fmt_hexmask(hw_map);
		info("job %u CPU input mask for node: %s",
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
		info("job %u CPU final HW mask for node: %s",
		     req->job_id, req->cpu_bind);
	} else {
		error("job %u allocated no CPUs",
		      req->job_id);
	}
	FREE_NULL_BITMAP(hw_map);
}

static int _validate_map(launch_tasks_request_msg_t *req, char *avail_mask,
			 char **err_msg)
{
	char *tmp_map, *save_ptr = NULL, *tok;
	cpu_set_t avail_cpus;
	bool superset = true;
	int rc = SLURM_SUCCESS;

	if (!req->cpu_bind) {
		char *err = "No list of CPU IDs provided to --cpu-bind=map_cpu:<list>";
		error("%s", err);
		if (err_msg)
			xstrfmtcat(*err_msg, "%s", err);
		return ESLURMD_CPU_BIND_ERROR;
	}

	CPU_ZERO(&avail_cpus);
	if (task_str_to_cpuset(&avail_cpus, avail_mask)) {
		char *err = "Failed to convert avail_mask into hex for CPU bind map";
		error("%s", err);
		if (err_msg)
			xstrfmtcat(*err_msg, "%s", err);
		return ESLURMD_CPU_BIND_ERROR;
	}

	tmp_map = xstrdup(req->cpu_bind);
	tok = strtok_r(tmp_map, ",", &save_ptr);
	while (tok) {
		int i = atoi(tok);
		if (!CPU_ISSET(i, &avail_cpus)) {
			/* The task's CPU map is completely invalid.
			 * Disable CPU map. */
			superset = false;
			break;
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp_map);

	if (!superset) {
		error("CPU binding outside of job step allocation, allocated CPUs are: %s.",
		      avail_mask);
		if (err_msg)
			xstrfmtcat(*err_msg, "CPU binding outside of job step allocation, allocated CPUs are: %s.",
				   avail_mask);
		rc = ESLURMD_CPU_BIND_ERROR;
	}
	return rc;
}

static int _validate_mask(launch_tasks_request_msg_t *req, char *avail_mask,
			  char **err_msg)
{
	char *new_mask = NULL, *save_ptr = NULL, *tok;
	cpu_set_t avail_cpus, task_cpus;
	bool superset = true;
	int rc = SLURM_SUCCESS;

	if (!req->cpu_bind) {
		char *err = "No list of CPU masks provided to --cpu-bind=mask_cpu:<list>";
		error("%s", err);
		if (err_msg)
			xstrfmtcat(*err_msg, "%s", err);
		return ESLURMD_CPU_BIND_ERROR;
	}

	CPU_ZERO(&avail_cpus);
	if (task_str_to_cpuset(&avail_cpus, avail_mask)) {
		char *err = "Failed to convert avail_mask into hex for CPU bind mask";
		error("%s", err);
		if (err_msg)
			xstrfmtcat(*err_msg, "%s", err);
		return ESLURMD_CPU_BIND_ERROR;
	}

	tok = strtok_r(req->cpu_bind, ",", &save_ptr);
	while (tok) {
		int i, overlaps = 0;
		char mask_str[CPU_SET_HEX_STR_SIZE];
		CPU_ZERO(&task_cpus);
		if (task_str_to_cpuset(&task_cpus, tok)) {
			char *err = "Failed to convert cpu bind string into hex for CPU bind mask";
			error("%s", err);
			if (err_msg)
				xstrfmtcat(*err_msg, "%s", err);
			xfree(new_mask);
			return ESLURMD_CPU_BIND_ERROR;
		}
		for (i = 0; i < CPU_SETSIZE; i++) {
			if (!CPU_ISSET(i, &task_cpus))
				continue;
			if (CPU_ISSET(i, &avail_cpus)) {
				overlaps++;
			} else {
				CPU_CLR(i, &task_cpus);
				superset = false;
			}
		}
		if (overlaps == 0) {
			/* The task's CPU mask is completely invalid.
			 * Give it all allowed CPUs. */
			for (i = 0; i < CPU_SETSIZE; i++) {
				if (CPU_ISSET(i, &avail_cpus))
					CPU_SET(i, &task_cpus);
			}
		}
		task_cpuset_to_str(&task_cpus, mask_str);
		if (new_mask)
			xstrcat(new_mask, ",");
		xstrcat(new_mask, mask_str);
		tok = strtok_r(NULL, ",", &save_ptr);
	}

	if (!superset) {
		error("CPU binding outside of job step allocation, allocated CPUs are: %s.",
		      avail_mask);
		if (err_msg)
			xstrfmtcat(*err_msg, "CPU binding outside of job step allocation, allocated CPUs are: %s.",
				   avail_mask);
		rc = ESLURMD_CPU_BIND_ERROR;
	}

	xfree(req->cpu_bind);
	req->cpu_bind = new_mask;
	return rc;
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
 * IN/OUT req - job launch request (cpu_bind_type and cpu_bind updated)
 * IN node_id - global task id array
 * OUT err_msg - optional string to pass out error message.
 */
extern int lllp_distribution(launch_tasks_request_msg_t *req, uint32_t node_id,
			     char **err_msg)
{
	int rc = SLURM_SUCCESS;
	bitstr_t **masks = NULL;
	char buf_type[100];
	int maxtasks = req->tasks_to_launch[node_id];
	int whole_nodes, whole_sockets, whole_cores, whole_threads;
	int part_sockets, part_cores;
	const uint32_t *gtid = req->global_task_ids[node_id];
	static uint16_t bind_entity =
		CPU_BIND_TO_THREADS | CPU_BIND_TO_CORES |
		CPU_BIND_TO_SOCKETS | CPU_BIND_TO_LDOMS;
	static uint16_t bind_mode =
		CPU_BIND_NONE | CPU_BIND_MASK |
		CPU_BIND_RANK | CPU_BIND_MAP |
		CPU_BIND_LDMASK | CPU_BIND_LDRANK |
		CPU_BIND_LDMAP;
	static int only_one_thread_per_core = -1;

	if (only_one_thread_per_core == -1) {
		if (conf->cpus == (conf->sockets * conf->cores))
			only_one_thread_per_core = 1;
		else
			only_one_thread_per_core = 0;
	}

	/*
	 * If we are telling the system we only want to use 1 thread
	 * per core with the CPUs node option this is the easiest way
	 * to portray that to the affinity plugin.
	 */
	if (only_one_thread_per_core)
		req->cpu_bind_type |= CPU_BIND_ONE_THREAD_PER_CORE;

	if (req->cpu_bind_type & bind_mode) {
		/* Explicit step binding specified by user */
		char *avail_mask = _alloc_mask(req,
					       &whole_nodes,  &whole_sockets,
					       &whole_cores,  &whole_threads,
					       &part_sockets, &part_cores);
		if (!avail_mask) {
			error("Could not determine allocated CPUs");
			if (err_msg)
				xstrfmtcat(*err_msg, "Could not determine allocated CPUs");
			rc = ESLURMD_CPU_BIND_ERROR;
		} else if ((whole_nodes == 0) &&
			   (req->job_core_spec == NO_VAL16) &&
			   (!(req->cpu_bind_type & CPU_BIND_MAP)) &&
			   (!(req->cpu_bind_type & CPU_BIND_MASK))) {

			if (!(req->cpu_bind_type & CPU_BIND_NONE)) {
				rc = ESLURMD_CPU_BIND_ERROR;
				slurm_sprint_cpu_bind_type(buf_type,
							   req->cpu_bind_type);
				error("Entire node must be allocated for %s",
				      buf_type);
				if (err_msg)
					xstrfmtcat(*err_msg, "Entire node must be allocated for %s",
						   buf_type);
			}
			xfree(req->cpu_bind);
			req->cpu_bind = avail_mask;
			req->cpu_bind_type &= (~bind_mode);
			req->cpu_bind_type |= CPU_BIND_MASK;
		} else {
			if (req->job_core_spec == NO_VAL16) {
				if (req->cpu_bind_type & CPU_BIND_MASK)
					rc = _validate_mask(req, avail_mask,
							    err_msg);
				else if (req->cpu_bind_type & CPU_BIND_MAP)
					rc = _validate_map(req, avail_mask,
							   err_msg);
			}
			xfree(avail_mask);
		}
		slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
		info("JobId=%u manual binding: %s",
		     req->step_id.job_id, buf_type);
		return rc;
	}

	if (!(req->cpu_bind_type & bind_entity)) {
		/*
		 * No bind unit (sockets, cores) specified by user,
		 * pick something reasonable
		 */
		bool auto_def_set = false;
		int spec_thread_cnt = 0;
		int max_tasks = req->tasks_to_launch[node_id] *
			req->cpus_per_task;
		char *avail_mask = _alloc_mask(req,
					       &whole_nodes,  &whole_sockets,
					       &whole_cores,  &whole_threads,
					       &part_sockets, &part_cores);
		debug("binding tasks:%d to nodes:%d sockets:%d:%d cores:%d:%d threads:%d",
		      max_tasks, whole_nodes, whole_sockets,
		      part_sockets, whole_cores, part_cores, whole_threads);
		if ((req->job_core_spec != NO_VAL16) &&
		    (req->job_core_spec &  CORE_SPEC_THREAD)  &&
		    (req->job_core_spec != CORE_SPEC_THREAD)) {
			spec_thread_cnt = req->job_core_spec &
				(~CORE_SPEC_THREAD);
		}
		if (((max_tasks == whole_sockets) && (part_sockets == 0)) ||
		    (spec_thread_cnt &&
		     (max_tasks == (whole_sockets + part_sockets)))) {
			req->cpu_bind_type |= CPU_BIND_TO_SOCKETS;
			goto make_auto;
		}
		if (((max_tasks == whole_cores) && (part_cores == 0)) ||
		    (spec_thread_cnt &&
		     (max_tasks == (whole_cores + part_cores)))) {
			req->cpu_bind_type |= CPU_BIND_TO_CORES;
			goto make_auto;
		}
		if (max_tasks == whole_threads) {
			req->cpu_bind_type |= CPU_BIND_TO_THREADS;
			goto make_auto;
		}

		if (slurm_conf.task_plugin_param & CPU_AUTO_BIND_TO_THREADS) {
			auto_def_set = true;
			req->cpu_bind_type |= CPU_BIND_TO_THREADS;
			goto make_auto;
		} else if (slurm_conf.task_plugin_param &
			   CPU_AUTO_BIND_TO_CORES) {
			auto_def_set = true;
			req->cpu_bind_type |= CPU_BIND_TO_CORES;
			goto make_auto;
		} else if (slurm_conf.task_plugin_param &
			   CPU_AUTO_BIND_TO_SOCKETS) {
			auto_def_set = true;
			req->cpu_bind_type |= CPU_BIND_TO_SOCKETS;
			goto make_auto;
		}

		if (avail_mask) {
			xfree(req->cpu_bind);
			req->cpu_bind = avail_mask;
			req->cpu_bind_type |= CPU_BIND_MASK;
		}

		slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
		info("JobId=%u auto binding off: %s",
		     req->step_id.job_id, buf_type);
		return rc;

	make_auto: xfree(avail_mask);
		slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
		info("JobId=%u %s auto binding: %s, dist %d",
		     req->step_id.job_id,
		     (auto_def_set) ? "default" : "implicit",
		     buf_type, req->task_dist);
	} else {
		/* Explicit bind unit (sockets, cores) specified by user */
		slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
		info("JobId=%u binding: %s, dist %d",
		     req->step_id.job_id, buf_type, req->task_dist);
	}

	switch (req->task_dist & SLURM_DIST_NODESOCKMASK) {
	case SLURM_DIST_BLOCK_BLOCK:
	case SLURM_DIST_CYCLIC_BLOCK:
	case SLURM_DIST_PLANE:
		debug2("JobId=%u will use lllp_block",
		       req->step_id.job_id);
		/* tasks are distributed in blocks within a plane */
		rc = _task_layout_lllp_block(req, node_id, &masks);
		break;
	case SLURM_DIST_ARBITRARY:
	case SLURM_DIST_BLOCK:
	case SLURM_DIST_CYCLIC:
	case SLURM_DIST_UNKNOWN:
		if (slurm_conf.select_type_param & CR_CORE_DEFAULT_DIST_BLOCK) {
			debug2("JobId=%u will use lllp_block because of SelectTypeParameters",
			       req->step_id.job_id);
			rc = _task_layout_lllp_block(req, node_id, &masks);
			break;
		}
		/*
		 * We want to fall through here if we aren't doing a
		 * default dist block.
		 */
	default:
		debug2("JobId=%u will use lllp_cyclic because of SelectTypeParameters",
		       req->step_id.job_id);
		rc = _task_layout_lllp_cyclic(req, node_id, &masks);
		break;
	}

	/*
	 * FIXME: I'm worried about core_bitmap with CPU_BIND_TO_SOCKETS &
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

		if (req->flags & LAUNCH_OVERCOMMIT) {
			/*
			 * Allow the step to run despite not being able to
			 * distribute tasks.
			 * e.g. Overcommit will fail to distribute tasks because
			 * the step has wants more cpus than allocated.
			 */
			rc = SLURM_SUCCESS;
		} else if (err_msg) {
			slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
			xstrfmtcat(*err_msg, "JobId=%u failed to distribute tasks (bind_type:%s) - this should never happen",
				   req->step_id.job_id, buf_type);
			error("%s", *err_msg);
		}
	}
	if (masks)
		_lllp_free_masks(maxtasks, masks);
	return rc;
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

/*
 * Determine which CPUs a job step can use.
 * OUT whole_<entity>_count - returns count of whole <entities> in this
 *                            allocation for this node
 * OUT part__<entity>_count - returns count of partial <entities> in this
 *                            allocation for this node
 * RET - a string representation of the available mask or NULL on error
 * NOTE: Caller must xfree() the return value.
 */
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

	alloc_bitmap = _get_avail_map(req->cred, &sockets, &cores, &threads);
	if (!alloc_bitmap)
		return NULL;

	alloc_mask = bit_alloc(bit_size(alloc_bitmap));

	i = 0;
	for (s = 0, s_miss = false; s < sockets; s++) {
		for (c = 0, c_hit = c_miss = false; c < cores; c++) {
			for (t = 0, t_hit = t_miss = false; t < threads; t++) {
				/*
				 * If we are pretending we have a larger system
				 * than we really have this is needed to make
				 * sure we don't bust the bank.
				 */
				if (i >= bit_size(alloc_bitmap))
					i = 0;
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
	FREE_NULL_BITMAP(alloc_bitmap);

	if ((req->job_core_spec != NO_VAL16) &&
	    (req->job_core_spec &  CORE_SPEC_THREAD)  &&
	    (req->job_core_spec != CORE_SPEC_THREAD)) {
		int spec_thread_cnt;
		spec_thread_cnt = req->job_core_spec & (~CORE_SPEC_THREAD);
		for (t = threads - 1;
		     ((t > 0) && (spec_thread_cnt > 0)); t--) {
			for (c = cores - 1;
			     ((c > 0) && (spec_thread_cnt > 0)); c--) {
				for (s = sockets - 1;
				     ((s >= 0) && (spec_thread_cnt > 0)); s--) {
					i = s * cores + c;
					i = (i * threads) + t;
					/*
					 * If config_overrides is used bitmap
					 * may be too small for the counter
					 */
					i %= conf->block_map_size;
					bit_clear(alloc_mask, i);
					spec_thread_cnt--;
				}
			}
		}
	}

	/* translate abstract masks to actual hardware layout */
	_lllp_map_abstract_masks(1, &alloc_mask);

#ifdef HAVE_NUMA
	if (req->cpu_bind_type & CPU_BIND_TO_LDOMS) {
		_match_masks_to_ldom(1, &alloc_mask);
	}
#endif

	str_mask = bit_fmt_hexmask(alloc_mask);
	FREE_NULL_BITMAP(alloc_mask);
	return str_mask;
}

/*
 * Given a job step request, return an equivalent local bitmap for this node
 * IN cred         - The job step launch request credential
 * OUT hw_sockets  - number of actual sockets on this node
 * OUT hw_cores    - number of actual cores per socket on this node
 * OUT hw_threads  - number of actual threads per core on this node
 * RET: bitmap of processors available to this job step on this node
 *      OR NULL on error
 */
static bitstr_t *_get_avail_map(slurm_cred_t *cred, uint16_t *hw_sockets,
				uint16_t *hw_cores, uint16_t *hw_threads)
{
	bitstr_t *req_map, *hw_map;
	uint16_t p, t, new_p, num_cpus, sockets, cores;
	int job_node_id;
	int start;
	char *str;
	int spec_thread_cnt = 0;
	slurm_cred_arg_t *arg = slurm_cred_get_args(cred);

	*hw_sockets = conf->sockets;
	*hw_cores   = conf->cores;
	*hw_threads = conf->threads;
	/* we need this node's ID in relation to the whole
	 * job allocation, not just this jobstep */
	job_node_id = nodelist_find(arg->job_hostlist, conf->node_name);
	if ((job_node_id < 0) || (job_node_id > arg->job_nhosts)) {
		error("%s: missing node %s in job credential (%s)",
		      __func__, conf->node_name, arg->job_hostlist);
		slurm_cred_unlock_args(cred);
		return NULL;
	}
	start = _get_local_node_info(arg, job_node_id, &sockets, &cores);
	debug3("slurmctld s %u c %u; hw s %u c %u t %u",
	       sockets, cores, *hw_sockets, *hw_cores, *hw_threads);

	num_cpus = MIN((sockets * cores), ((*hw_sockets)*(*hw_cores)));
	req_map = (bitstr_t *) bit_alloc(num_cpus);
	hw_map  = (bitstr_t *) bit_alloc(conf->block_map_size);

	/* Transfer core_bitmap data to local req_map.
	 * The MOD function handles the case where fewer processes
	 * physically exist than are configured (slurmd is out of
	 * sync with the slurmctld daemon). */
	for (p = 0; p < (sockets * cores); p++) {
		if (bit_test(arg->step_core_bitmap, start + p))
			bit_set(req_map, (p % num_cpus));
	}

	str = (char *)bit_fmt_hexmask(req_map);
	debug3("%ps core mask from slurmctld: %s",
	       &arg->step_id, str);
	xfree(str);

	for (p = 0; p < num_cpus; p++) {
		if (bit_test(req_map, p) == 0)
			continue;
		/* If we are pretending we have a larger system than
		   we really have this is needed to make sure we
		   don't bust the bank.
		*/
		new_p = p % conf->block_map_size;
		/* core_bitmap does not include threads, so we
		 * add them here but limit them to what the job
		 * requested */
		for (t = 0; t < (*hw_threads); t++) {
			uint16_t bit = new_p * (*hw_threads) + t;
			bit %= conf->block_map_size;
			bit_set(hw_map, bit);
		}
	}

	if ((arg->job_core_spec != NO_VAL16) &&
	    (arg->job_core_spec &  CORE_SPEC_THREAD)  &&
	    (arg->job_core_spec != CORE_SPEC_THREAD)) {
		spec_thread_cnt = arg->job_core_spec & (~CORE_SPEC_THREAD);
	}
	if (spec_thread_cnt) {
		/* Skip specialized threads as needed */
		int i, t, c, s;
		for (t = conf->threads - 1;
		     ((t >= 0) && (spec_thread_cnt > 0)); t--) {
			for (c = conf->cores - 1;
			     ((c >= 0) && (spec_thread_cnt > 0)); c--) {
				for (s = conf->sockets - 1;
				     ((s >= 0) && (spec_thread_cnt > 0)); s--) {
					i = s * conf->cores + c;
					i = (i * conf->threads) + t;
					/*
					 * If config_overrides is used bitmap
					 * may be too small for the counter
					 */
					i %= conf->block_map_size;
					bit_clear(hw_map, i);
					spec_thread_cnt--;
				}
			}
		}
	}

	str = (char *)bit_fmt_hexmask(hw_map);
	debug3("%ps CPU final mask for local node: %s",
	       &arg->step_id, str);
	xfree(str);

	FREE_NULL_BITMAP(req_map);
	slurm_cred_unlock_args(cred);
	return hw_map;
}

/* helper function for _expand_masks() */
static void _blot_mask(bitstr_t *mask, bitstr_t *avail_map, uint16_t blot)
{
	uint16_t i, j, size = 0;
	int prev = -1;

	if (!mask)
		return;
	size = bit_size(mask);
	for (i = 0; i < size; i++) {
		if (bit_test(mask, i)) {
			/* fill in this blot */
			uint16_t start = (i / blot) * blot;
			if (start != prev) {
				for (j = start; j < start + blot; j++) {
					if (bit_test(avail_map, j))
						bit_set(mask, j);
				}
				prev = start;
			}
		}
	}
}

/* helper function for _expand_masks()
 * for each task, consider which other bits are set in avail_map
 * on the same socket */
static void _blot_mask_sockets(const uint32_t maxtasks, const uint32_t task,
			       bitstr_t **masks, uint16_t hw_sockets,
			       uint16_t hw_cores, uint16_t hw_threads,
			       bitstr_t *avail_map)
{
  	uint16_t i, j, size = 0;
	int blot;

	if (!masks[task])
 		return;

	blot = bit_size(avail_map) / hw_sockets;
	if (blot <= 0)
		blot = 1;
	size = bit_size(masks[task]);
	for (i = 0; i < size; i++) {
		if (bit_test(masks[task], i)) {
			/* check if other bits are set in avail_map on this
			 * socket and set each corresponding bit in masks */
			uint16_t start = (i / blot) * blot;
			for (j = start; j < start+blot; j++) {
				if (bit_test(avail_map, j))
					bit_set(masks[task], j);
			}
		}
	}
}

/* for each mask, expand the mask around the set bits to include the
 * complete resource to which the set bits are to be bound */
static void _expand_masks(uint16_t cpu_bind_type, const uint32_t maxtasks,
			  bitstr_t **masks, uint16_t hw_sockets,
			  uint16_t hw_cores, uint16_t hw_threads,
			  bitstr_t *avail_map)
{
	uint32_t i;

	if (cpu_bind_type & CPU_BIND_TO_THREADS)
		return;
	if (cpu_bind_type & CPU_BIND_TO_CORES) {
		if (hw_threads < 2)
			return;
		for (i = 0; i < maxtasks; i++) {
			_blot_mask(masks[i], avail_map, hw_threads);
		}
		return;
	}
	if (cpu_bind_type & CPU_BIND_TO_SOCKETS) {
		if (hw_threads*hw_cores < 2)
			return;
		for (i = 0; i < maxtasks; i++) {
			_blot_mask_sockets(maxtasks, i, masks, hw_sockets,
					   hw_cores, hw_threads, avail_map);
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
 * is the same as the Cyclic distribution performed in srun.
 *
 *  Distribution at the lllp:
 *  -m hostfile|block|cyclic:block|cyclic
 *
 * The first distribution "hostfile|block|cyclic" is computed
 * in srun. The second distribution "block|cyclic" is computed
 * locally by each slurmd.
 *
 * The input to the lllp distribution algorithms is the gids (tasks
 * ids) generated for the local node.
 *
 * The output is a mapping of the gids onto logical processors
 * (thread/core/socket) with is expressed cpu_bind masks.
 *
 * If a task asks for more than one CPU per task, put the tasks as
 * close as possible (fill core rather than going next socket for the
 * extra task)
 *
 */
static int _task_layout_lllp_cyclic(launch_tasks_request_msg_t *req,
				    uint32_t node_id, bitstr_t ***masks_p)
{
	int last_taskcount = -1, taskcount = 0;
	uint16_t i, s, hw_sockets = 0, hw_cores = 0, hw_threads = 0;
	uint16_t offset = 0, p = 0;
	int size, max_tasks = req->tasks_to_launch[node_id];
	int max_cpus = max_tasks * req->cpus_per_task;
	bitstr_t *avail_map;
	bitstr_t **masks = NULL;
	int *socket_last_pu = NULL;
	int core_inx, pu_per_core, *core_tasks = NULL, *core_threads = NULL;
	int req_threads_per_core = 0;

	info ("_task_layout_lllp_cyclic ");

	avail_map = _get_avail_map(req->cred, &hw_sockets, &hw_cores,
				   &hw_threads);
	if (!avail_map)
		return ESLURMD_CPU_LAYOUT_ERROR;

	if (req->threads_per_core && (req->threads_per_core != NO_VAL16))
		req_threads_per_core = req->threads_per_core;
	else if (req->cpu_bind_type & CPU_BIND_ONE_THREAD_PER_CORE)
		req_threads_per_core = 1;

	size = bit_set_count(avail_map);
	if (req_threads_per_core) {
		if (size < (req->cpus_per_task * (hw_threads /
						  req_threads_per_core))) {
			error("only %d bits in avail_map, threads_per_core requires %d!",
			      size,
			      (req->cpus_per_task * (hw_threads /
						     req_threads_per_core)));
			FREE_NULL_BITMAP(avail_map);
			return ESLURMD_CPU_LAYOUT_ERROR;
		}
	}
	if (size < max_tasks) {
		if (!(req->flags & LAUNCH_OVERCOMMIT))
			error("only %d bits in avail_map for %d tasks!",
			      size, max_tasks);
		FREE_NULL_BITMAP(avail_map);
		return ESLURMD_CPU_LAYOUT_ERROR;
	}
	if (size < max_cpus) {
		/* Possible result of overcommit */
		i = size / max_tasks;
		info("reset cpus_per_task from %d to %d",
		     req->cpus_per_task, i);
		req->cpus_per_task = i;
	}

	pu_per_core = hw_threads;
	core_tasks = xmalloc(sizeof(int) * hw_sockets * hw_cores);
	core_threads = xmalloc(sizeof(int) * hw_sockets * hw_cores);
	socket_last_pu = xmalloc(hw_sockets * sizeof(int));

	*masks_p = xmalloc(max_tasks * sizeof(bitstr_t*));
	masks = *masks_p;

	size = bit_size(avail_map);

	offset = hw_cores * hw_threads;
	s = 0;
	while (taskcount < max_tasks) {
		if (taskcount == last_taskcount) {
			error("_task_layout_lllp_cyclic failure");
			FREE_NULL_BITMAP(avail_map);
			xfree(core_tasks);
			xfree(core_threads);
			xfree(socket_last_pu);
			return ESLURMD_CPU_LAYOUT_ERROR;
		}
		last_taskcount = taskcount;
		for (i = 0; i < size; i++) {
			bool already_switched = false;
			uint16_t bit;
			uint16_t orig_s = s;

			while (socket_last_pu[s] >= offset) {
				/* Switch to the next socket we have
				 * ran out here. */

				/* This only happens if the slurmctld
				 * gave us an allocation that made a
				 * task split sockets.  Or if the
				 * entire allocation is on one socket.
				 */
				s = (s + 1) % hw_sockets;
				if (orig_s == s) {
					/* This should rarely happen,
					 * but is here for sanity sake.
					 */
					debug("allocation is full, "
					      "oversubscribing");
					memset(core_tasks, 0,
					       (sizeof(int) *
					        hw_sockets * hw_cores));
					memset(core_threads, 0,
					       (sizeof(int) *
					        hw_sockets * hw_cores));
					memset(socket_last_pu, 0,
					       (sizeof(int) * hw_sockets));
				}
			}

			bit = socket_last_pu[s] + (s * offset);

			/* In case hardware and config differ */
			bit %= size;

			/* set up for the next one */
			socket_last_pu[s]++;

			if (!bit_test(avail_map, bit))
				continue;

			core_inx = bit / pu_per_core;
			if ((req->ntasks_per_core != 0) &&
			    (core_tasks[core_inx] >= req->ntasks_per_core))
				continue;
			if (req_threads_per_core &&
			    (core_threads[core_inx] >= req_threads_per_core))
				continue;

			if (!masks[taskcount])
				masks[taskcount] =
					bit_alloc(conf->block_map_size);

			//info("setting %d %d", taskcount, bit);
			bit_set(masks[taskcount], bit);

			if (!already_switched &&
			    (((req->task_dist & SLURM_DIST_NODESOCKMASK) ==
			      SLURM_DIST_CYCLIC_CFULL) ||
			     ((req->task_dist & SLURM_DIST_NODESOCKMASK) ==
			      SLURM_DIST_BLOCK_CFULL))) {
				/* This means we are laying out cpus
				 * within a task cyclically as well. */
				s = (s + 1) % hw_sockets;
				already_switched = true;
			}

			core_threads[core_inx]++;

			if (++p < req->cpus_per_task)
				continue;

			core_tasks[core_inx]++;

			/* Binding to cores, skip remaining of the threads */
			if ((req->cpu_bind_type & CPU_BIND_TO_CORES) ||
			    (req->ntasks_per_core == 1)) {
				int threads_not_used;
				if (req->cpus_per_task < hw_threads)
					threads_not_used =
						hw_threads - req->cpus_per_task;
				else
					threads_not_used =
						req->cpus_per_task % hw_threads;
				socket_last_pu[s] += threads_not_used;
			}
			p = 0;

			if (!already_switched) {
				/* Now that we have finished a task, switch to
				 * the next socket. */
				s = (s + 1) % hw_sockets;
			}

			if (++taskcount >= max_tasks)
				break;
		}
	}

	/* last step: expand the masks to bind each task
	 * to the requested resource */
	_expand_masks(req->cpu_bind_type, max_tasks, masks,
		      hw_sockets, hw_cores, hw_threads, avail_map);
	FREE_NULL_BITMAP(avail_map);
	xfree(core_tasks);
	xfree(core_threads);
	xfree(socket_last_pu);

	return SLURM_SUCCESS;
}

/*
 * _task_layout_lllp_block
 *
 * task_layout_lllp_block will create a block distribution at the
 * lowest level of logical processor which is either socket, core or
 * thread depending on the system architecture. The Block algorithm
 * is the same as the Block distribution performed in srun.
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
	int c, i, size, last_taskcount = -1, taskcount = 0;
	uint16_t hw_sockets = 0, hw_cores = 0, hw_threads = 0;
	int max_tasks = req->tasks_to_launch[node_id];
	int max_cpus = max_tasks * req->cpus_per_task;
	bitstr_t *avail_map;
	bitstr_t **masks = NULL;
	int core_inx, pu_per_core, *core_tasks = NULL, *core_threads = NULL;
	int sock_inx, pu_per_socket, *socket_tasks = NULL;
	int req_threads_per_core = 0;

	info("_task_layout_lllp_block ");

	avail_map = _get_avail_map(req->cred, &hw_sockets, &hw_cores,
				   &hw_threads);
	if (!avail_map) {
		return ESLURMD_CPU_LAYOUT_ERROR;
	}

	if (req->threads_per_core && (req->threads_per_core != NO_VAL16))
		req_threads_per_core = req->threads_per_core;
	else if (req->cpu_bind_type & CPU_BIND_ONE_THREAD_PER_CORE)
		req_threads_per_core = 1;

	size = bit_set_count(avail_map);
	if (req_threads_per_core) {
		if (size < (req->cpus_per_task * (hw_threads /
						  req_threads_per_core))) {
			error("only %d bits in avail_map, threads_per_core requires %d!",
			      size,
			      (req->cpus_per_task * (hw_threads /
						     req_threads_per_core)));
			FREE_NULL_BITMAP(avail_map);
			return ESLURMD_CPU_LAYOUT_ERROR;
		}
	}
	if (size < max_tasks) {
		if (!(req->flags & LAUNCH_OVERCOMMIT))
			error("only %d bits in avail_map for %d tasks!",
			      size, max_tasks);
		FREE_NULL_BITMAP(avail_map);
		return ESLURMD_CPU_LAYOUT_ERROR;
	}
	if (size < max_cpus) {
		/* Possible result of overcommit */
		i = size / max_tasks;
		info("reset cpus_per_task from %d to %d",
		     req->cpus_per_task, i);
		req->cpus_per_task = i;
	}
	size = bit_size(avail_map);

	*masks_p = xmalloc(max_tasks * sizeof(bitstr_t*));
	masks = *masks_p;

	pu_per_core = hw_threads;
	core_tasks = xmalloc(sizeof(int) * hw_sockets * hw_cores);
	core_threads = xmalloc(sizeof(int) * hw_sockets * hw_cores);
	pu_per_socket = hw_cores * hw_threads;
	socket_tasks = xmalloc(sizeof(int) * hw_sockets);

	/* block distribution with oversubsciption */
	c = 0;
	while (taskcount < max_tasks) {
		if (taskcount == last_taskcount) {
			error("_task_layout_lllp_block infinite loop");
			FREE_NULL_BITMAP(avail_map);
			xfree(core_tasks);
			xfree(core_threads);
			xfree(socket_tasks);
			return ESLURMD_CPU_LAYOUT_ERROR;
		}
		if (taskcount > 0) {
			/* Clear counters to over-subscribe, if necessary */
			memset(core_tasks, 0,
			       (sizeof(int) * hw_sockets * hw_cores));
			memset(core_threads, 0,
			       (sizeof(int) * hw_sockets * hw_cores));
			memset(socket_tasks, 0,
			       (sizeof(int) * hw_sockets));
		}
		last_taskcount = taskcount;
		/* the abstract map is already laid out in block order,
		 * so just iterate over it
		 */
		for (i = 0; i < size; i++) {
			/* skip unavailable resources */
			if (bit_test(avail_map, i) == 0)
				continue;

			core_inx = i / pu_per_core;
			if ((req->ntasks_per_core != 0) &&
			    (core_tasks[core_inx] >= req->ntasks_per_core))
				continue;
			sock_inx = i / pu_per_socket;
			if ((req->ntasks_per_socket != 0) &&
			    (socket_tasks[sock_inx] >= req->ntasks_per_socket))
				continue;
			if (req_threads_per_core &&
			    (core_threads[core_inx] >= req_threads_per_core))
				continue;

			if (!masks[taskcount])
				masks[taskcount] = bit_alloc(
					conf->block_map_size);
			//info("setting %d %d", taskcount, i);
			bit_set(masks[taskcount], i);

			core_threads[core_inx]++;

			if (++c < req->cpus_per_task)
				continue;

			/* We found one! Increment the count on each unit */
			core_tasks[core_inx]++;
			socket_tasks[sock_inx]++;

			/* Binding to cores, skip remaining of the threads */
			if ((req->cpu_bind_type & CPU_BIND_TO_CORES) ||
			    (req->ntasks_per_core == 1)) {
				int threads_not_used;
				if (req->cpus_per_task < hw_threads)
					threads_not_used =
						hw_threads - req->cpus_per_task;
				else
					threads_not_used =
						req->cpus_per_task % hw_threads;
				i += threads_not_used;
			}
			c = 0;
			if (++taskcount >= max_tasks)
				break;
		}
	}
	xfree(core_tasks);
	xfree(core_threads);
	xfree(socket_tasks);

	/* last step: expand the masks to bind each task
	 * to the requested resource */
	_expand_masks(req->cpu_bind_type, max_tasks, masks,
		      hw_sockets, hw_cores, hw_threads, avail_map);
	FREE_NULL_BITMAP(avail_map);

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
			if (bit < bit_size(newmask))
				bit_set(newmask, bit);
			else
				error("can't go from %d -> %d since we "
				      "only have %"BITSTR_FMT" bits",
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
			FREE_NULL_BITMAP(bitmask);
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
	int i, num_bits = 0, masks_len;
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

	debug3("%d %"BITSTR_FMT" %d", maxtasks, charsize,
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
			masks_str[masks_len - 1] = ',';
		strlcpy(&masks_str[masks_len], str, curlen);
		masks_len += curlen;
		xfree(str);
	}

	if (req->cpu_bind) {
		xfree(req->cpu_bind);
	}
	if (masks_str[0] != '\0') {
		req->cpu_bind = masks_str;
		masks_str = NULL;
		req->cpu_bind_type |= CPU_BIND_MASK;
	} else {
		req->cpu_bind = NULL;
		req->cpu_bind_type &= ~CPU_BIND_VERBOSE;
	}
	xfree(masks_str);

	/* clear mask generation bits */
	req->cpu_bind_type &= ~CPU_BIND_TO_THREADS;
	req->cpu_bind_type &= ~CPU_BIND_TO_CORES;
	req->cpu_bind_type &= ~CPU_BIND_TO_SOCKETS;
	req->cpu_bind_type &= ~CPU_BIND_TO_LDOMS;

	slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
	info("_lllp_generate_cpu_bind jobid [%u]: %s, %s",
	     req->step_id.job_id, buf_type, req->cpu_bind);
}
