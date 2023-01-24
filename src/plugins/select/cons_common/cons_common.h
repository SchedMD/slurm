/*****************************************************************************\
 *  cons_common.h - Common function interface for the select/cons_* plugins
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

#ifndef _CONS_COMMON_H
#define _CONS_COMMON_H

#include "core_array.h"
#include "job_test.h"
#include "node_data.h"
#include "part_data.h"
#include "job_resources.h"

#include "src/interfaces/gres.h"
#include "src/slurmctld/slurmctld.h"

typedef struct avail_res {	/* Per-node resource availability */
	uint16_t avail_cpus;	/* Count of available CPUs for this job
				   limited by options like --ntasks-per-node */
	uint16_t avail_gpus;	/* Count of available GPUs */
	uint16_t avail_res_cnt;	/* Count of available CPUs + GPUs */
	uint16_t *avail_cores_per_sock;	/* Per-socket available core count */
	uint32_t gres_min_cores; /* Minimum number of cores to satisfy GRES
				    constraints */
	uint16_t max_cpus;	/* Maximum available CPUs on the node */
	uint16_t min_cpus;	/* Minimum allocated CPUs */
	uint16_t sock_cnt;	/* Number of sockets on this node */
	List sock_gres_list;	/* Per-socket GRES availability, sock_gres_t */
	uint16_t spec_threads;	/* Specialized threads to be reserved */
	uint16_t tpc;		/* Threads/cpus per core */
} avail_res_t;

struct select_nodeinfo {
	uint16_t magic;		/* magic number */
	uint16_t alloc_cpus;
	uint64_t alloc_memory;
	uint64_t *tres_alloc_cnt;	/* array of tres counts allocated.
					   NOT PACKED */
	char     *tres_alloc_fmt_str;	/* formatted str of allocated tres */
	double    tres_alloc_weighted;	/* weighted number of tres allocated. */
};

typedef struct {
	avail_res_t *(*can_job_run_on_node)(job_record_t *job_ptr,
					    bitstr_t **core_map,
					    const uint32_t node_i,
					    uint32_t s_p_n,
					    node_use_record_t *node_usage,
					    uint16_t cr_type,
					    bool test_only, bool will_run,
					    bitstr_t **part_core_map);
	int (*choose_nodes)(job_record_t *job_ptr, bitstr_t *node_map,
			    bitstr_t **avail_core, uint32_t min_nodes,
			    uint32_t max_nodes, uint32_t req_nodes,
			    avail_res_t **avail_res_array, uint16_t cr_type,
			    bool prefer_alloc_nodes,
			    gres_mc_data_t *tres_mc_ptr);
	int (*dist_tasks_compute_c_b)(job_record_t *job_ptr,
				      uint32_t *gres_task_limit,
				      uint32_t *gres_min_cpus);
	bitstr_t **(*mark_avail_cores)(bitstr_t *node_map, uint16_t core_spec);
	bitstr_t *(*pick_first_cores)(bitstr_t *avail_node_bitmap,
				      uint32_t node_cnt,
				      uint32_t *core_cnt,
				      bitstr_t ***exc_cores);
	bitstr_t *(*sequential_pick)(bitstr_t *avail_node_bitmap,
				     uint32_t node_cnt,
				     uint32_t *core_cnt,
				     bitstr_t ***exc_cores);
	void (*spec_core_filter)(bitstr_t *node_bitmap, bitstr_t **avail_cores);
} cons_common_callbacks_t;

/* Global common variables */
extern bool     backfill_busy_nodes;
extern int      bf_window_scale;
extern cons_common_callbacks_t cons_common_callbacks;
extern int      core_array_size;
extern bool     gang_mode;
extern bool     have_dragonfly;
extern bool     is_cons_tres;
extern const uint16_t nodeinfo_magic;
extern bool     pack_serial_at_end;
extern const uint32_t plugin_id;
extern bool     preempt_by_part;
extern bool     preempt_by_qos;
extern uint16_t priority_flags;
extern bool     spec_cores_first;
extern bool     topo_optional;

extern char *common_node_state_str(uint16_t node_state);

/*
 * Get configured DefCpuPerGPU information from a list
 * (either global or per partition list)
 * Returns NO_VAL64 if configuration parameter not set
 */
extern uint64_t common_get_def_cpu_per_gpu(List job_defaults_list);

/*
 * Get configured DefMemPerGPU information from a list
 * (either global or per partition list)
 * Returns NO_VAL64 if configuration parameter not set
 */
extern uint64_t common_get_def_mem_per_gpu(List job_defaults_list);

extern void common_free_avail_res(avail_res_t *avail_res);

/* Determine how many cpus per core we can use */
extern uint16_t common_cpus_per_core(job_details_t *details, int node_inx);

extern void common_init(void);
extern void common_fini(void);

/*
 * Bit a core bitmap array of available cores
 * node_bitmap IN - Nodes available for use
 * core_spec IN - Specialized core specification, NO_VAL16 if none
 * RET core bitmap array, one per node. Use free_core_array() to release memory
 */
extern bitstr_t **common_mark_avail_cores(
	bitstr_t *node_bitmap, uint16_t core_spec);

/*
 * common_allocate - Given the job requirements, determine which resources
 *                   from the given node can be allocated (if any) to this
 *                   job. Returns the number of cpus that can be used by
 *                   this node AND a bitmap of the selected cores|sockets.
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - core_bitmap of available cores on this node
 * IN part_core_map - bitmap of cores already allocated on this partition/node
 * IN node_i        - index of node to be evaluated
 * OUT cpu_alloc_size - minimum allocation size, in CPUs
 * IN req_sock_map - OPTIONAL bitmap of required sockets
 * IN cr_type - Consumable Resource setting
 * RET resource availability structure, call _free_avail_res() to free
 */
extern avail_res_t *common_allocate(job_record_t *job_ptr,
				    bitstr_t *core_map,
				    bitstr_t *part_core_map,
				    const uint32_t node_i,
				    int *cpu_alloc_size,
				    bitstr_t *req_sock_map,
				    uint16_t cr_type);

#endif /* _CONS_COMMON_H */
