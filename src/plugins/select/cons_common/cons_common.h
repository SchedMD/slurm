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
#include "src/common/gres.h"
#include "src/slurmctld/slurmctld.h"

/* a partition's per-row core allocation bitmap arrays (1 bitmap per node) */
struct part_row_data {
	bitstr_t *first_row_bitmap;	/* Pointer to first element in
					 * row_bitmap */
	struct job_resources **job_list;/* List of jobs in this row */
	uint32_t job_list_size;		/* Size of job_list array */
	uint32_t num_jobs;		/* Number of occupied entries in
					 * job_list array */
	bitstr_t **row_bitmap;		/* contains core bitmap for all jobs in
					 * this row, one bitstr_t for each node
					 * In cons_res only the first ptr is
					 * used.
					 */
	int row_bitmap_size;            /* size of row_bitmap array */
};

/* partition core allocation bitmap arrays (1 bitmap per node) */
struct part_res_record {
	struct part_res_record *next; /* Ptr to next part_res_record */
	uint16_t num_rows;	      /* Number of elements in "row" array */
	struct part_record *part_ptr; /* controller part record pointer */
	struct part_row_data *row;    /* array of rows containing jobs */
};

/* per-node resource data */
struct node_res_record {
	uint16_t boards; 	      /* count of boards configured */
	uint16_t cores;		      /* count of cores per socket configured */
	uint16_t cpus;		      /* count of logical processors
				       * configured */
	uint32_t cume_cores;	      /* total cores for all nodes through us */
	uint64_t mem_spec_limit;      /* MB of specialized/system memory */
	struct node_record *node_ptr; /* ptr to the actual node */
	uint64_t real_memory;	      /* MB of real memory configured */
	uint16_t sockets;	      /* count of sockets per board configured*/
	uint16_t threads;	      /* count of hyperthreads per core */
	uint16_t tot_cores;	      /* total cores per node */
	uint16_t tot_sockets;	      /* total sockets per node */
	uint16_t vpus;		      /* count of virtual processors configure
				       * this could be the physical threads
				       * count or could be the core count if
				       * the node's cpu count matches the
				       * core count */
};

/* per-node resource usage record */
struct node_use_record {
	uint64_t alloc_memory;	      /* real memory reserved by already
				       * scheduled jobs */
	List gres_list;		      /* list of gres_node_state_t records as
				       * defined in in src/common/gres.h.
				       * Local data used only in state copy
				       * to emulate future node state */
	uint16_t node_state;	      /* see node_cr_state comments */
};

typedef struct avail_res {	/* Per-node resource availability */
	uint16_t avail_cpus;	/* Count of available CPUs */
	uint16_t avail_gpus;	/* Count of available GPUs */
	uint16_t avail_res_cnt;	/* Count of available CPUs + GPUs */
	uint16_t *avail_cores_per_sock;	/* Per-socket available core count */
	uint16_t max_cpus;	/* Maximum available CPUs */
	uint16_t min_cpus;	/* Minimum allocated CPUs */
	uint16_t sock_cnt;	/* Number of sockets on this node */
	List sock_gres_list;	/* Per-socket GRES availability, sock_gres_t */
	uint16_t spec_threads;	/* Specialized threads to be reserved */
	uint16_t vpus;		/* Virtual processors (CPUs) per core */
} avail_res_t;


typedef struct {
	void (*add_job_to_res)(job_resources_t *job_resrcs_ptr,
			       struct part_row_data *r_ptr,
			       const uint16_t *bits_per_node);
	/* can_job_fit_row - function to test for conflicting core bitmap
	 * elements */
	int (*can_job_fit_in_row)(struct job_resources *job,
				  struct part_row_data *r_ptr);
	avail_res_t *(*can_job_run_on_node)(struct job_record *job_ptr,
					    bitstr_t **core_map,
					    const uint32_t node_i,
					    uint32_t s_p_n,
					    struct node_use_record *node_usage,
					    uint16_t cr_type,
					    bool test_only,
					    bitstr_t **part_core_map);
	int (*choose_nodes)(struct job_record *job_ptr, bitstr_t *node_map,
			    bitstr_t **avail_core, uint32_t min_nodes,
			    uint32_t max_nodes, uint32_t req_nodes,
			    avail_res_t **avail_res_array, uint16_t cr_type,
			    bool prefer_alloc_nodes,
			    gres_mc_data_t *tres_mc_ptr);
	int (*verify_node_state)(struct part_res_record *cr_part_ptr,
				 struct job_record *job_ptr,
				 bitstr_t *node_bitmap,
				 uint16_t cr_type,
				 struct node_use_record *node_usage,
				 enum node_cr_state job_node_req,
				 bitstr_t **exc_cores, bool qos_preemptor);
	bitstr_t **(*mark_avail_cores)(bitstr_t *node_map, uint16_t core_spec);
	int (*cr_dist)(struct job_record *job_ptr, const uint16_t cr_type,
		       bool preempt_mode, bitstr_t **core_array,
		       uint32_t *gres_task_limit);
	void (*build_row_bitmaps)(struct part_res_record *p_ptr,
				  struct job_record *job_ptr);
} cons_common_callbacks_t;

/* Global common variables */
extern bool     backfill_busy_nodes;
extern int      bf_window_scale;
extern cons_common_callbacks_t cons_common_callbacks;
extern int      core_array_size;
extern uint16_t cr_type;
extern bool     gang_mode;
extern bool     have_dragonfly;
extern bool     is_cons_tres;
extern bool     pack_serial_at_end;
extern const uint32_t plugin_id;
extern const char *plugin_type;
extern bool     preempt_by_part;
extern bool     preempt_by_qos;
extern uint16_t priority_flags;
extern uint64_t select_debug_flags;
extern uint16_t select_fast_schedule;
extern int      select_node_cnt;
extern bool     spec_cores_first;
extern bool     topo_optional;
extern const char *plugin_type;

extern struct part_res_record *select_part_record;
extern struct node_res_record *select_node_record;
extern struct node_use_record *select_node_usage;

/* Delete the given select_node_record and select_node_usage arrays */
extern void common_destroy_node_data(struct node_use_record *node_usage,
				     struct node_res_record *node_data);

/* Delete the given list of partition data */
extern void common_destroy_part_data(struct part_res_record *this_ptr);

/* Delete the given partition row data */
extern void common_destroy_row_data(
	struct part_row_data *row, uint16_t num_rows);

extern void common_free_avail_res(avail_res_t *avail_res);
extern void common_free_avail_res_array(avail_res_t **avail_res_array);

/* Determine how many cpus per core we can use */
extern int common_cpus_per_core(struct job_details *details, int node_inx);

/*
 * Add job resource use to the partition data structure
 */
extern void common_add_job_to_row(struct job_resources *job,
				  struct part_row_data *r_ptr);

/*
 * allocate resources to the given job
 * - add 'struct job_resources' resources to 'struct part_res_record'
 * - add job's memory requirements to 'struct node_res_record'
 *
 * if action = 0 then add cores, memory + GRES (starting new job)
 * if action = 1 then add memory + GRES (adding suspended job at restart)
 * if action = 2 then only add cores (suspended job is resumed)
 *
 *
 * See also: common_rm_job_res()
 */
extern int common_add_job_to_res(struct job_record *job_ptr, int action);

/*
 * Deallocate resources previously allocated to the given job
 * - subtract 'struct job_resources' resources from 'struct part_res_record'
 * - subtract job's memory requirements from 'struct node_res_record'
 *
 * if action = 0 then subtract cores, memory + GRES (running job was terminated)
 * if action = 1 then subtract memory + GRES (suspended job was terminated)
 * if action = 2 then only subtract cores (job is suspended)
 * IN: job_fini - job fully terminating on this node (not just a test)
 *
 * RET SLURM_SUCCESS or error code
 *
 * See also: common_add_job_to_res()
 */
extern int common_rm_job_res(struct part_res_record *part_record_ptr,
			     struct node_use_record *node_usage,
			     struct job_record *job_ptr, int action,
			     bool job_fini);

/* Log contents of partition structure */
extern void common_dump_parts(struct part_res_record *p_ptr);

/* Clear all elements the row_bitmap of the row */
extern void common_clear_row_bitmap(struct part_row_data *r_ptr);

/* sort the rows of a partition from "most allocated" to "least allocated" */
extern void common_sort_part_rows(struct part_res_record *p_ptr);

/* Create a duplicate part_res_record list */
extern struct part_res_record *common_dup_part_data(
	struct part_res_record *orig_ptr);

/* Create a duplicate part_row_data struct */
extern struct part_row_data *common_dup_row_data(struct part_row_data *orig_row,
						 uint16_t num_rows);

extern void common_init(void);
extern void common_fini(void);
extern int common_reconfig(void);

/* This is Part 1 of a 4-part procedure which can be found in
 * src/slurmctld/read_config.c. The whole story goes like this:
 *
 * Step 1: common_node_init          : initializes the global node arrays
 * Step 2: common_state_restore      : NO-OP - nothing to restore
 * Step 3: common_job_init           : NO-OP - nothing to initialize
 * Step 4: common_select_nodeinfo_set: called from reset_job_bitmaps() with
 *                                     each valid recovered job_ptr AND from
 *                                     select_nodes(), this procedure adds
 *                                     job data to the 'select_part_record'
 *                                     global array
 */
extern int common_node_init(struct node_record *node_ptr, int node_cnt);

/* Determine if a job can ever run */
extern int common_test_only(struct job_record *job_ptr, bitstr_t *node_bitmap,
			    uint32_t min_nodes, uint32_t max_nodes,
			    uint32_t req_nodes, uint16_t job_node_req);

/*
 * Determine where and when the job at job_ptr can begin execution by updating
 * a scratch cr_record structure to reflect each job terminating at the
 * end of its time limit and use this to show where and when the job at job_ptr
 * will begin execution. Used by Slurm's sched/backfill plugin.
 */
extern int common_will_run_test(struct job_record *job_ptr,
				bitstr_t *node_bitmap,
				uint32_t min_nodes, uint32_t max_nodes,
				uint32_t req_nodes, uint16_t job_node_req,
				List preemptee_candidates,
				List *preemptee_job_list,
				bitstr_t **exc_core_bitmap);

/* Allocate resources for a job now, if possible */
extern int common_run_now(struct job_record *job_ptr, bitstr_t *node_bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  uint32_t req_nodes, uint16_t job_node_req,
			  List preemptee_candidates, List *preemptee_job_list,
			  bitstr_t **exc_cores);

/*
 * common_allocate_cores - Given the job requirements, determine which cores
 *                   from the given node can be allocated (if any) to this
 *                   job. Returns the number of cpus that can be used by
 *                   this node AND a bitmap of the selected cores.
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - core_bitmap of available cores on this node
 * IN part_core_map - bitmap of cores already allocated on this partition/node
 * IN node_i        - index of node to be evaluated
 * IN/OUT cpu_alloc_size - minimum allocation size, in CPUs
 * IN cpu_type      - if true, allocate CPUs rather than cores
 * IN req_sock_map - OPTIONAL bitmap of required sockets
 * RET resource availability structure, call _free_avail_res() to free
 */
extern avail_res_t *common_allocate_cores(struct job_record *job_ptr,
					  bitstr_t *core_map,
					  bitstr_t *part_core_map,
					  const uint32_t node_i,
					  int *cpu_alloc_size,
					  bool cpu_type,
					  bitstr_t *req_sock_map);

/*
 * common_allocate_sockets - Given the job requirements, determine which sockets
 *                     from the given node can be allocated (if any) to this
 *                     job. Returns the number of cpus that can be used by
 *                     this node AND a core-level bitmap of the selected
 *                     sockets.
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - core_bitmap of available cores on this node
 * IN part_core_map - bitmap of cores already allocated on this partition/node
 * IN node_i        - index of node to be evaluated
 * IN/OUT cpu_alloc_size - minimum allocation size, in CPUs
 * IN req_sock_map - OPTIONAL bitmap of required sockets
 * RET resource availability structure, call _free_avail_res() to free
 */
extern avail_res_t *common_allocate_sockets(struct job_record *job_ptr,
					    bitstr_t *core_map,
					    bitstr_t *part_core_map,
					    const uint32_t node_i,
					    int *cpu_alloc_size,
					    bitstr_t *req_sock_map);


#endif /* _CONS_COMMON_H */
