/*****************************************************************************\
 *  select_cons_res.c - node selection plugin supporting consumable
 *  resources policies.
 *****************************************************************************\
 *
 *  The following example below illustrates how four jobs are allocated
 *  across a cluster using when a processor consumable resource approach.
 *
 *  The example cluster is composed of 4 nodes (10 cpus in total):
 *  linux01 (with 2 processors),
 *  linux02 (with 2 processors),
 *  linux03 (with 2 processors), and
 *  linux04 (with 4 processors).
 *
 *  The four jobs are the following:
 *  1. srun -n 4 -N 4  sleep 120 &
 *  2. srun -n 3 -N 3 sleep 120 &
 *  3. srun -n 1 sleep 120 &
 *  4. srun -n 3 sleep 120 &
 *  The user launches them in the same order as listed above.
 *
 *  Using a processor consumable resource approach we get the following
 *  job allocation and scheduling:
 *
 *  The output of squeue shows that we have 3 out of the 4 jobs allocated
 *  and running. This is a 2 running job increase over the default Slurm
 *  approach.
 *
 *  Job 2, Job 3, and Job 4 are now running concurrently on the cluster.
 *
 *  [<snip>]# squeue
 *  JOBID PARTITION     NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     5        lsf    sleep     root  PD       0:00      1 (Resources)
 *     2        lsf    sleep     root   R       0:13      4 linux[01-04]
 *     3        lsf    sleep     root   R       0:09      3 linux[01-03]
 *     4        lsf    sleep     root   R       0:05      1 linux04
 *  [<snip>]#
 *
 *  Once Job 2 finishes, Job 5, which was pending, is allocated
 *  available resources and is then running as illustrated below:
 *
 *  [<snip>]# squeue
 *   JOBID PARTITION    NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     3        lsf    sleep     root   R       1:58      3 linux[01-03]
 *     4        lsf    sleep     root   R       1:54      1 linux04
 *     5        lsf    sleep     root   R       0:02      3 linux[01-03]
 *  [<snip>]#
 *
 *  Job 3, Job 4, and Job 5 are now running concurrently on the cluster.
 *
 *  [<snip>]#  squeue
 *  JOBID PARTITION     NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     5        lsf    sleep     root   R       1:52      3 linux[01-03]
 *  [<snip>]#
 *
 * The advantage of the consumable resource scheduling policy is that
 * the job throughput can increase dramatically.
 *
 *****************************************************************************
 *  Copyright (C) 2005-2008 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle <susanne.balle@hp.com>, who borrowed heavily
 *  from select/linear
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

#include "config.h"

#define _GNU_SOURCE

#include <inttypes.h>
#include <string.h>

#include "src/common/slurm_xlator.h"
#include "src/common/slurm_selecttype_info.h"
#include "src/common/assoc_mgr.h"
#include "src/common/xstring.h"
#include "select_cons_res.h"

#include "dist_tasks.h"
#include "job_test.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "select" for Slurm node selection) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load select plugins if the plugin_type string has a
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Consumable Resources (CR) Node Selection plugin";
const char plugin_type[] = "select/cons_res";
const uint32_t plugin_id      = SELECT_PLUGIN_CONS_RES;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const uint32_t pstate_version = 7;	/* version control on saved state */
const uint16_t nodeinfo_magic = 0x82aa;

/* Given available node and core bitmaps, remove all specialized cores
 * node_bitmap IN - Nodes available for use
 * core_bitmap IN/OUT - Cores currently NOT available for use */
static void _spec_core_filter(bitstr_t *node_bitmap, bitstr_t **core_bitmap)
{
	bitstr_t **avail_core_map =
		common_mark_avail_cores(node_bitmap, NO_VAL16);

	xassert(core_bitmap);

	if (*core_bitmap) {
		bit_or_not(*core_bitmap, *avail_core_map);
	} else {
		bit_not(*avail_core_map);
		*core_bitmap = *avail_core_map;
		*avail_core_map = NULL;
	}

	free_core_array(&avail_core_map);
}

/* Once here, if core_cnt is NULL, avail_bitmap has nodes not used by any job or
 * reservation */
bitstr_t *_sequential_pick(bitstr_t *avail_bitmap, uint32_t node_cnt,
			   uint32_t *core_cnt, bitstr_t ***core_bitmap_p)
{
	bitstr_t *sp_avail_bitmap;
	char str[300];
	uint32_t cores_per_node = 0, extra_cores_needed = 0;
	bitstr_t *tmpcore;
	int total_core_cnt = 0;
	bitstr_t **core_bitmap = *core_bitmap_p;

	/* We have these cases here:
	 *	1) Reservation requests using just number of nodes
	 *		- core_cnt is null
	 *	2) Reservations request using number of nodes + number of cores
	 *	3) Reservations request using node list
	 *		- node_cnt is 0
	 *		- core_cnt is null
	 *	4) Reservation request using node list + number of cores list
	 *		- node_cnt is 0
	 */

	if ((node_cnt) && (core_cnt)) {
		total_core_cnt = core_cnt[0];
		cores_per_node = core_cnt[0] / MAX(node_cnt, 1);
		log_flag(RESERVATION, "Reserving %u cores across %d nodes",
			 total_core_cnt, node_cnt);
		extra_cores_needed = total_core_cnt -
				     (cores_per_node * node_cnt);
	}
	if ((!node_cnt) && (core_cnt)) {
		int num_nodes = bit_set_count(avail_bitmap);
		int i;
		if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
			bit_fmt(str, (sizeof(str) - 1), avail_bitmap);
			log_flag(RESERVATION, "Reserving cores from nodes: %s",
				 str);
		}
		for (i = 0; (i < num_nodes) && core_cnt[i]; i++)
			total_core_cnt += core_cnt[i];
	}


	sp_avail_bitmap = bit_alloc(bit_size(avail_bitmap));

	if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
		bit_fmt(str, (sizeof(str) - 1), avail_bitmap);
		log_flag(RESERVATION, "Reservations requires %d cores (%u each on %d nodes, plus %u), avail bitmap:%s ",
			 total_core_cnt,
			 cores_per_node,
			 node_cnt,
			 extra_cores_needed,
			 str);

	}

	if (core_cnt) { /* Reservation is using partial nodes */
		int node_list_inx = 0;

		xassert(core_bitmap);

		tmpcore = bit_copy(*core_bitmap);

		bit_not(tmpcore); /* tmpcore contains now current free cores */
		if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
			bit_fmt(str, (sizeof(str) - 1), tmpcore);
			log_flag(RESERVATION,"Reservation is using partial nodes. Free cores (whole cluster) are: %s",
				 str);
		}
		bit_and(*core_bitmap, tmpcore);	/* clear core_bitmap */

		while (total_core_cnt) {
			int inx, coff, coff2;
			int i;
			int cores_in_node;
			int local_cores;

			if (node_cnt == 0) {
				cores_per_node = core_cnt[node_list_inx];
				if (cores_per_node == 0)
					break;
			}

			inx = bit_ffs(avail_bitmap);
			if (inx < 0)
				break;
			log_flag(RESERVATION, "Using node %d", inx);

			coff = cr_get_coremap_offset(inx);
			coff2 = cr_get_coremap_offset(inx + 1);
			local_cores = coff2 - coff;

			bit_clear(avail_bitmap, inx);

			if (local_cores < cores_per_node) {
				log_flag(RESERVATION, "Skip node %d (local: %d, needed: %d)",
					 inx, local_cores, cores_per_node);
				continue;
			}

			cores_in_node = 0;

			/* First let's see in there are enough cores in
			 * this node */
			for (i = 0; i < local_cores; i++) {
				if (bit_test(tmpcore, coff + i))
					cores_in_node++;
			}
			if (cores_in_node < cores_per_node) {
				log_flag(RESERVATION, "Skip node %d (avail: %d, needed: %d)",
					 inx,
					 cores_in_node,
					 cores_per_node);
				continue;
			}

			log_flag(RESERVATION, "Using node %d (avail: %d, needed: %d)",
				 inx, cores_in_node, cores_per_node);

			cores_in_node = 0;
			for (i = 0; i < local_cores; i++) {
				if (bit_test(tmpcore, coff + i)) {
					bit_set(*core_bitmap, coff + i);
					total_core_cnt--;
					cores_in_node++;
					if (cores_in_node > cores_per_node)
						extra_cores_needed--;
					if ((total_core_cnt == 0) ||
					    ((extra_cores_needed == 0) &&
					     (cores_in_node >= cores_per_node)))
						break;
				}
			}

			if (cores_in_node) {
				/* Add this node to the final node bitmap */
				log_flag(RESERVATION, "Reservation using %d cores in node %d",
					 cores_in_node, inx);
				bit_set(sp_avail_bitmap, inx);
			} else {
				log_flag(RESERVATION, "Reservation NOT using node %d",
					 inx);
			}
			node_list_inx++;
		}
		FREE_NULL_BITMAP(tmpcore);

		if (total_core_cnt) {
			log_flag(RESERVATION, "reservation request can not be satisfied");
			FREE_NULL_BITMAP(sp_avail_bitmap);
			return NULL;
		}

		if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
                       bit_fmt(str, (sizeof(str) - 1), *core_bitmap);
                       log_flag(RESERVATION,
				"sequential pick using coremap: %s", str);
		}


	} else { /* Reservation is using full nodes */
		while (node_cnt) {
			int inx;

			inx = bit_ffs(avail_bitmap);
			if (inx < 0)
				break;

			/* Add this node to the final node bitmap */
			bit_set(sp_avail_bitmap, inx);
			node_cnt--;

			/* Clear this node from the initial available bitmap */
			bit_clear(avail_bitmap, inx);
		}

		if (node_cnt) {
			log_flag(RESERVATION, "reservation request can not be satisfied");
			FREE_NULL_BITMAP(sp_avail_bitmap);
			return NULL;
		}

		if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
			bit_fmt(str, (sizeof(str) - 1), sp_avail_bitmap);
			log_flag(RESERVATION, "sequential pick using nodemap: %s",
				 str);
		}
	}

	return sp_avail_bitmap;
}

bitstr_t *_pick_first_cores(bitstr_t *avail_bitmap, uint32_t node_cnt,
			    uint32_t *core_cnt, bitstr_t ***core_bitmap_p)
{
	bitstr_t *sp_avail_bitmap;
	bitstr_t *tmpcore;
	int inx, jnx, first_node, last_node;
	int node_offset = 0;
	int coff, coff2, local_cores;
	bitstr_t **core_bitmap = *core_bitmap_p;

	if (!core_cnt || (core_cnt[0] == 0))
		return NULL;

	sp_avail_bitmap = bit_alloc(bit_size(avail_bitmap));

	xassert(core_bitmap);

	tmpcore = bit_copy(*core_bitmap);
	bit_not(tmpcore); /* tmpcore contains now current free cores */
	bit_and(*core_bitmap, tmpcore);	/* clear core_bitmap */

	first_node = bit_ffs(avail_bitmap);
	if (first_node >= 0)
		last_node  = bit_fls(avail_bitmap);
	else
		last_node = first_node - 1;
	for (inx = first_node; inx <= last_node; inx++) {
		coff = cr_get_coremap_offset(inx);
		coff2 = cr_get_coremap_offset(inx + 1);
		local_cores = coff2 - coff;

		bit_clear(avail_bitmap, inx);
		if (local_cores < core_cnt[node_offset])
			local_cores = -1;
		else
			local_cores = core_cnt[node_offset];
		for (jnx = 0; jnx < local_cores; jnx++) {
			if (!bit_test(tmpcore, coff + jnx))
				break;
			bit_set(*core_bitmap, coff + jnx);
		}
		if (jnx < core_cnt[node_offset])
			continue;
		local_cores = coff2 - coff;
		for (jnx = core_cnt[node_offset]; jnx < local_cores; jnx++) {
			bit_clear(tmpcore, coff + jnx);
		}
		bit_set(sp_avail_bitmap, inx);
		if (core_cnt[++node_offset] == 0)
			break;
	}

	FREE_NULL_BITMAP(tmpcore);
	if (core_cnt[node_offset]) {
		log_flag(RESERVATION, "reservation request can not be satisfied");
		FREE_NULL_BITMAP(sp_avail_bitmap);
	}

	return sp_avail_bitmap;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	common_init();

	cons_common_callbacks.can_job_run_on_node = can_job_run_on_node;
	cons_common_callbacks.choose_nodes = choose_nodes;
	cons_common_callbacks.dist_tasks_compute_c_b = dist_tasks_compute_c_b;
	cons_common_callbacks.pick_first_cores = _pick_first_cores;
	cons_common_callbacks.sequential_pick = _sequential_pick;
	cons_common_callbacks.spec_core_filter = _spec_core_filter;

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	common_fini();

	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard Slurm
 * node selection API.
 */

/* select_p_state_save() in cons_common */

/* select_p_state_restore() in cons_common */

/* select_p_job_init() in cons_common */

/* select_p_node_init() in cons_common */

/*
 * select_p_job_test - Given a specification of scheduling requirements,
 *	identify the nodes which "best" satisfy the request.
 * 	"best" is defined as either a minimal number of consecutive nodes
 *	or if sharing resources then sharing them with a job of similar size.
 * IN/OUT job_ptr - pointer to job being considered for initiation,
 *                  set's start_time when job expected to start
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN req_nodes - requested (or desired) count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * IN mode - SELECT_MODE_RUN_NOW   (0): try to schedule job now
 *           SELECT_MODE_TEST_ONLY (1): test if job can ever run
 *           SELECT_MODE_WILL_RUN  (2): determine when and where job can run
 * IN preemptee_candidates - List of pointers to jobs which can be preempted.
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 * RET zero on success, EINVAL otherwise
 * globals (passed via select_p_node_init):
 *	node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: the job information that is considered for scheduling includes:
 *	req_node_bitmap: bitmap of specific nodes required by the job
 *	contiguous: allocated nodes must be sequentially located
 *	num_cpus: minimum number of processors required by the job
 * NOTE: bitmap must be a superset of req_nodes at the time that
 *	select_p_job_test is called
 */
extern int select_p_job_test(job_record_t *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list,
			     bitstr_t *exc_core_bitmap)
{
	xassert(bitmap);

	debug2("%s for %pJ", __func__, job_ptr);

	if (!job_ptr->details)
		return EINVAL;

	return common_job_test(job_ptr, bitmap, min_nodes, max_nodes,
			       req_nodes, mode, preemptee_candidates,
			       preemptee_job_list, &exc_core_bitmap);
}

/* select_p_job_begin() in cons_common */

/* select_p_job_ready() in cons_common */

/* select_p_job_resized() in cons_common */

/* select_p_job_expand() in cons_common */

/* select_p_job_signal() in cons_common */

/* select_p_job_fini() in cons_common */

/* select_p_job_suspend() in cons_common */

/* select_p_job_resume() in cons_common */

/* select_p_step_pick_nodes() in cons_common */

/* select_p_step_start() in cons_common */

/* select_p_step_finish() in cons_common */

/* select_p_select_nodeinfo_pack() in cons_common */

/* select_p_select_nodeinfo_unpack() in cons_common */

/* select_p_select_nodeinfo_alloc() in cons_common */

/* select_p_select_nodeinfo_free() in cons_common */

/* select_p_select_nodeinfo_set_all() in cons_common */

/* select_p_select_nodeinfo_set() in cons_common */

/* select_p_select_nodeinfo_get() in cons_common */

/* select_p_select_jobinfo_alloc() in cons_common */

/* select_p_select_jobinfo_free() in cons_common */

/* select_p_select_jobinfo_set() in cons_common */

/* select_p_select_jobinfo_get() in cons_common */

/* select_p_select_jobinfo_copy() in cons_common */

/* select_p_select_jobinfo_pack() in cons_common */

/* select_p_select_jobinfo_unpack() in cons_common */

/* select_p_select_jobinfo_sprint() in cons_common */

/* select_p_select_jobinfo_xstrdup() in cons_common */

/* select_p_get_info_from_plugin() in cons_common */

/* select_p_reconfigure() in cons_common */

/* select_p_resv_test() in cons_common */
