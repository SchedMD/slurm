/*****************************************************************************\
 *  cons_helpers.c - Helper functions for the select/cons_tres plugin
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Derived in large part from select/cons_tres plugin
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

#include "src/common/slurm_xlator.h"

#include "select_cons_tres.h"

#include "src/common/assoc_mgr.h"
#include "src/interfaces/select.h"
#include "src/interfaces/topology.h"

/* Global variables */

/*
 * Get configured DefCpuPerGPU information from a list
 * (either global or per partition list)
 * Returns NO_VAL64 if configuration parameter not set
 */
extern uint64_t cons_helpers_get_def_cpu_per_gpu(list_t *job_defaults_list)
{
	uint64_t cpu_per_gpu = NO_VAL64;
	list_itr_t *iter;
	job_defaults_t *job_defaults;

	if (!job_defaults_list)
		return cpu_per_gpu;

	iter = list_iterator_create(job_defaults_list);
	while ((job_defaults = (job_defaults_t *) list_next(iter))) {
		if (job_defaults->type == JOB_DEF_CPU_PER_GPU) {
			cpu_per_gpu = job_defaults->value;
			break;
		}
	}
	list_iterator_destroy(iter);

	return cpu_per_gpu;
}

/*
 * Get configured DefMemPerGPU information from a list
 * (either global or per partition list)
 * Returns NO_VAL64 if configuration parameter not set
 */
extern uint64_t cons_helpers_get_def_mem_per_gpu(list_t *job_defaults_list)
{
	uint64_t mem_per_gpu = NO_VAL64;
	list_itr_t *iter;
	job_defaults_t *job_defaults;

	if (!job_defaults_list)
		return mem_per_gpu;

	iter = list_iterator_create(job_defaults_list);
	while ((job_defaults = (job_defaults_t *) list_next(iter))) {
		if (job_defaults->type == JOB_DEF_MEM_PER_GPU) {
			mem_per_gpu = job_defaults->value;
			break;
		}
	}
	list_iterator_destroy(iter);

	return mem_per_gpu;
}

extern bitstr_t **cons_helpers_mark_avail_cores(
	bitstr_t *node_bitmap, job_record_t *job_ptr)
{
	bitstr_t **avail_cores;
	int from_core, to_core, incr_core, from_sock, to_sock, incr_sock;
	int res_core, res_sock, res_off;
	int c;
	int rem_core_spec, node_core_spec, thread_spec = 0;
	node_record_t *node_ptr;
	bitstr_t *core_map = NULL;
	uint16_t use_spec_cores = slurm_conf.conf_flags & CONF_FLAG_ASRU;
	uint16_t core_spec = job_ptr->details->core_spec;
	uint32_t coff;
	bool req_gpu = false;
	uint32_t gpu_plugin_id = gres_get_gpu_plugin_id();

	if ((job_ptr->details->whole_node == 1) ||
	    (job_ptr->gres_list_req &&
	     list_find_first(job_ptr->gres_list_req,
			     gres_find_id, &gpu_plugin_id)))
		req_gpu = true;

	avail_cores = build_core_array();

	if ((core_spec != NO_VAL16) &&
	    (core_spec & CORE_SPEC_THREAD)) {	/* Reserving threads */
		thread_spec = core_spec & (~CORE_SPEC_THREAD);
		core_spec = NO_VAL16;		/* Don't remove cores */
	}

	for (int n = 0; (node_ptr = next_node_bitmap(node_bitmap, &n)); n++) {
		c    = 0;
		coff = node_ptr->tot_cores;
		avail_cores[n] = bit_alloc(node_ptr->tot_cores);
		core_map = avail_cores[n];

		if ((core_spec != NO_VAL16) &&
		    (core_spec >= node_ptr->tot_cores)) {
			bit_clear(node_bitmap, n);
			continue;
		}

		bit_nset(core_map, c, coff - 1);

		/*
		 * If the job isn't requesting a GPU we will remove those cores
		 * that are reserved for gpu jobs.
		 */
		if (node_ptr->gpu_spec_bitmap && !req_gpu) {
			for (int i = 0; i < node_ptr->tot_cores; i++) {
				if (!bit_test(node_ptr->gpu_spec_bitmap, i)) {
					bit_clear(core_map, c + i);
				}
			}
		}

		/* Job can't over-ride system defaults */
		if (use_spec_cores && (core_spec == 0))
			continue;

		if (thread_spec &&
		    (node_ptr->cpus == node_ptr->tot_cores))
			/* Each core has one thead, reserve cores here */
			node_core_spec = thread_spec;
		else
			node_core_spec = core_spec;

		/*
		 * remove node's specialized cores accounting toward the
		 * requested limit if allowed by configuration
		 */
		rem_core_spec = node_core_spec;
		if (node_ptr->node_spec_bitmap) {
			for (int i = 0; i < node_ptr->tot_cores; i++) {
				if (!bit_test(node_ptr->node_spec_bitmap, i)) {
					bit_clear(core_map, c + i);
					if (!use_spec_cores)
						continue;
					rem_core_spec--;
					if (!rem_core_spec)
						break;
				}
			}
		}

		if (!use_spec_cores || !rem_core_spec ||
		    (node_core_spec == NO_VAL16))
			continue;

		/* if more cores need to be specialized, look for
		 * them in the non-specialized cores */
		if (spec_cores_first) {
			from_core = 0;
			to_core   = node_ptr->cores;
			incr_core = 1;
			from_sock = 0;
			to_sock   = node_ptr->tot_sockets;
			incr_sock = 1;
		} else {
			from_core = node_ptr->cores - 1;
			to_core   = -1;
			incr_core = -1;
			from_sock = node_ptr->tot_sockets - 1;
			to_sock   = -1;
			incr_sock = -1;
		}
		for (res_core = from_core;
		     ((rem_core_spec > 0) && (res_core != to_core));
		     res_core += incr_core) {
			for (res_sock = from_sock;
			     ((rem_core_spec > 0) && (res_sock != to_sock));
			     res_sock += incr_sock) {
				res_off = c + res_core +
					(res_sock * node_ptr->cores);
				if (!bit_test(core_map, res_off))
					continue;
				bit_clear(core_map, res_off);
				rem_core_spec--;
			}
		}
	}

	return avail_cores;
}
