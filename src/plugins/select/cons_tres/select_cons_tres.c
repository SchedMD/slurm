/*****************************************************************************\
 *  select_cons_tres.c - Resource selection plugin supporting Trackable
 *  RESources (TRES) policies.
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
 *  Derived in large part from select/cons_res plugin
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
#include "src/common/assoc_mgr.h"
#include "src/common/slurm_selecttype_info.h"
#include "src/common/xstring.h"
#include "select_cons_tres.h"
#include "job_test.h"
#include "dist_tasks.h"

#define _DEBUG 0	/* Enables module specific debugging */
#define NODEINFO_MAGIC 0x8a5d

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
const char plugin_name[] = "Trackable RESources (TRES) Selection plugin";
const char plugin_type[] = "select/cons_tres";
const uint32_t plugin_id      = SELECT_PLUGIN_CONS_TRES;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const uint32_t pstate_version = 7;	/* version control on saved state */
const uint16_t nodeinfo_magic = 0x8a5d;

/* Global variables */

/* Clear from avail_cores all specialized cores */
static void _spec_core_filter(bitstr_t *node_bitmap, bitstr_t **avail_cores)
{
	bitstr_t **avail_core_map =
		common_mark_avail_cores(node_bitmap, NO_VAL16);

	xassert(avail_cores);

	core_array_not(avail_core_map);
	core_array_or(avail_cores, avail_core_map);
	free_core_array(&avail_core_map);
}

/*
 * Select resources for advanced reservation
 * avail_node_bitmap IN - Available nodes
 * node_cnt IN - required node count
 * core_cnt IN - required core count
 * exc_cores IN/OUT - Cores to AVOID using on input, selected cores on output
 * RET selected nodes
 */
static bitstr_t *_pick_first_cores(bitstr_t *avail_node_bitmap,
				   uint32_t node_cnt, uint32_t *core_cnt,
				   bitstr_t ***exc_cores)
{
	char tmp[128];
	bitstr_t **tmp_cores;
	bitstr_t **avail_cores;
	bitstr_t *picked_node_bitmap = NULL;
	bitstr_t *tmp_core_bitmap;
	int c, c_cnt, i;
	int local_node_offset = 0;
	bool fini = false;

	if (!core_cnt || (core_cnt[0] == 0))
		return picked_node_bitmap;

	if (*exc_cores == NULL) {	/* Exclude no cores by default */
		if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
			bit_fmt(tmp, sizeof(tmp), avail_node_bitmap);
			log_flag(RESERVATION, "exc_cores:NULL avail_nodes:%s",
				 tmp);
		}

		c = cr_get_coremap_offset(node_record_count);
		tmp_core_bitmap = bit_alloc(c);
		bit_not(tmp_core_bitmap);
		avail_cores = core_bitmap_to_array(tmp_core_bitmap);
		FREE_NULL_BITMAP(tmp_core_bitmap);
	} else {
		if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
			tmp_cores = *exc_cores;
			bit_fmt(tmp, sizeof(tmp), avail_node_bitmap);
			log_flag(RESERVATION, "avail_nodes:%s",
				 tmp);
			for (i = 0; i < node_record_count; i++) {
				if (!tmp_cores[i])
					continue;
				bit_fmt(tmp, sizeof(tmp), tmp_cores[i]);
				log_flag(RESERVATION, "exc_cores[%d]: %s",
					 i, tmp);
			}
		}
		/*
		 * Ensure all nodes in avail_node_bitmap are represented
		 * in exc_cores. For now include ALL nodes.
		 */
		c = cr_get_coremap_offset(node_record_count);
		tmp_core_bitmap = bit_alloc(c);
		bit_not(tmp_core_bitmap);
		avail_cores = core_bitmap_to_array(tmp_core_bitmap);
		FREE_NULL_BITMAP(tmp_core_bitmap);
		core_array_and_not(avail_cores, *exc_cores);
	}

	xassert(avail_cores);

	picked_node_bitmap = bit_alloc(node_record_count);
	for (i = 0; i < node_record_count; i++) {
		if (fini ||
		    !avail_cores[i] ||
		    !bit_test(avail_node_bitmap, i) ||
		    (bit_set_count_range(avail_cores[i], 0,
					 core_cnt[local_node_offset]) <
		     core_cnt[local_node_offset])) {
			FREE_NULL_BITMAP(avail_cores[i]);
			continue;
		}
		bit_set(picked_node_bitmap, i);
		c_cnt = 0;
		for (c = 0; c < node_record_table_ptr[i]->tot_cores; c++) {
			if (!bit_test(avail_cores[i], c))
				continue;
			if (++c_cnt > core_cnt[local_node_offset])
				bit_clear(avail_cores[i], c);
		}
		if (core_cnt[++local_node_offset] == 0)
			fini = true;
	}

	if (!fini) {
		log_flag(RESERVATION, "reservation request can not be satisfied");
		FREE_NULL_BITMAP(picked_node_bitmap);
		free_core_array(&avail_cores);
	} else {
		free_core_array(exc_cores);
		*exc_cores = avail_cores;

		if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
			for (i = 0; i < node_record_count; i++) {
				if (!avail_cores[i])
					continue;
				bit_fmt(tmp, sizeof(tmp), avail_cores[i]);
				log_flag(RESERVATION, "selected cores[%d] %s",
					 i, tmp);
			}
		}
	}

	return picked_node_bitmap;
}

/*
 * Select resources for advanced reservation
 * avail_node_bitmap IN - Available nodes
 * node_cnt IN - required node count
 * core_cnt IN - required core count
 * exc_cores IN/OUT - Cores to AVOID using on input, selected cores on output
 * RET selected node bitmap
 */
static bitstr_t *_sequential_pick(bitstr_t *avail_node_bitmap,
				  uint32_t node_cnt, uint32_t *core_cnt,
				  bitstr_t ***exc_cores)
{
#if _DEBUG
	char tmp[128];
	bitstr_t **tmp_cores;
#endif
	bitstr_t **avail_cores = NULL;
	bitstr_t *picked_node_bitmap;
	char str[300];
	int cores_per_node = 0, extra_cores_needed = -1;
	int total_core_cnt = 0, local_node_offset = 0, num_nodes;
	bitstr_t *tmp_core_bitmap;
	int c, c_cnt, c_target, i;
	bool fini = false, single_core_cnt = false;

	/*
	 * We have these cases here:
	 *	1) node_cnt != 0 && core_cnt != NULL
	 *	2) node_cnt == 0 && core_cnt != NULL
	 *	3) node_cnt != 0 && core_cnt == NULL
	 *	4) node_cnt == 0 && core_cnt == NULL
	 */
	if (core_cnt) {
		num_nodes = bit_set_count(avail_node_bitmap);
		for (i = 0; (i < num_nodes) && core_cnt[i]; i++)
			total_core_cnt += core_cnt[i];
		if ((node_cnt > 1) && (i == 1)) {
			/* single core_cnt element applied across all nodes */
			cores_per_node = MAX((total_core_cnt / node_cnt), 1);
			extra_cores_needed = total_core_cnt -
					     (cores_per_node * node_cnt);
		} else if ((node_cnt == 0) && (i == 1)) {
			/*
			 * single core_cnt element applied across arbitrary
			 * node count
			 */
			single_core_cnt = true;
		}
	}
#if _DEBUG
	if (cores_per_node) {
		info("Reservations requires %d cores (%u each on %u nodes, plus %d)",
		     total_core_cnt, cores_per_node,
		     node_cnt, extra_cores_needed);
	} else if (single_core_cnt) {
		info("Reservations requires %d cores total",
		     total_core_cnt);
	} else if (core_cnt && core_cnt[0]) {
		info("Reservations requires %d cores with %d cores on first node",
		     total_core_cnt, core_cnt[0]);
	} else {
		info("Reservations requires %u nodes total",
		     node_cnt);
	}
#endif

	picked_node_bitmap = bit_alloc(node_record_count);
	if (core_cnt) { /* Reservation is using partial nodes */
		debug2("Reservation is using partial nodes");
		if (*exc_cores == NULL) {      /* Exclude no cores by default */
#if _DEBUG
			bit_fmt(tmp, sizeof(tmp), avail_node_bitmap);
			info("avail_nodes:%s", tmp);
			info("exc_cores: NULL");
#endif
			c = cr_get_coremap_offset(node_record_count);
			tmp_core_bitmap = bit_alloc(c);
			bit_not(tmp_core_bitmap);
			avail_cores = core_bitmap_to_array(tmp_core_bitmap);
			FREE_NULL_BITMAP(tmp_core_bitmap);
		} else {
#if _DEBUG
			tmp_cores = *exc_cores;
			bit_fmt(tmp, sizeof(tmp), avail_node_bitmap);
			info("avail_nodes:%s", tmp);
			for (i = 0; i < node_record_count; i++) {
				if (!tmp_cores[i])
					continue;
				bit_fmt(tmp, sizeof(tmp), tmp_cores[i]);
				info("exc_cores[%d]: %s", i, tmp);
			}
#endif
			/*
			 * Ensure all nodes in avail_node_bitmap are represented
			 * in exc_cores. For now include ALL nodes.
			 */
			c = cr_get_coremap_offset(node_record_count);
			tmp_core_bitmap = bit_alloc(c);
			bit_not(tmp_core_bitmap);
			avail_cores = core_bitmap_to_array(tmp_core_bitmap);
			FREE_NULL_BITMAP(tmp_core_bitmap);
			core_array_and_not(avail_cores, *exc_cores);
		}
		xassert(avail_cores);

		for (i = 0; i < node_record_count; i++) {
			if (fini || !avail_cores[i] ||
			    !bit_test(avail_node_bitmap, i)) {
				FREE_NULL_BITMAP(avail_cores[i]);
				continue;
			}
			c = bit_set_count(avail_cores[i]);
			if (cores_per_node) {
				if (c < cores_per_node)
					continue;
				if ((c > cores_per_node) &&
				    (extra_cores_needed > 0)) {
					c_cnt = cores_per_node +
						extra_cores_needed;
					if (c_cnt > c)
						c_target = c;
					else
						c_target = c_cnt;
					extra_cores_needed -= (c_target - c);
				} else {
					c_target = cores_per_node;
				}
			} else if (single_core_cnt) {
				if (c > total_core_cnt)
					c_target = total_core_cnt;
				else
					c_target = c;
				total_core_cnt -= c_target;
			} else { /* !single_core_cnt */
				if (c < core_cnt[local_node_offset])
					continue;
				c_target = core_cnt[local_node_offset];
			}
			c_cnt = 0;
			for (c = 0; c < node_record_table_ptr[i]->tot_cores;
			     c++) {
				if (!bit_test(avail_cores[i], c))
					continue;
				if (c_cnt >= c_target)
					bit_clear(avail_cores[i], c);
				else
					c_cnt++;
			}
			if (c_cnt) {
				bit_set(picked_node_bitmap, i);
				node_cnt--;
			}
			if (cores_per_node) {		/* Test node count */
				if (node_cnt <= 0)
					fini = true;
			} else if (single_core_cnt) {	/* Test core count */
				if (total_core_cnt <= 0)
					fini = true;
			} else {		       /* Test core_cnt array */
				if (core_cnt[++local_node_offset] == 0)
					fini = true;
			}
		}

		if (!fini) {
			info("reservation request can not be satisfied");
			FREE_NULL_BITMAP(picked_node_bitmap);
			free_core_array(&avail_cores);
		} else {
			free_core_array(exc_cores);
			*exc_cores = avail_cores;
		}
	} else { /* Reservation is using full nodes */
		while (node_cnt) {
			int inx;

			inx = bit_ffs(avail_node_bitmap);
			if (inx < 0)
				break;

			/* Add this node to the final node bitmap */
			bit_set(picked_node_bitmap, inx);
			node_cnt--;

			/* Clear this node from the initial available bitmap */
			bit_clear(avail_node_bitmap, inx);
		}

		if (node_cnt) {
			info("Reservation request can not be satisfied");
			FREE_NULL_BITMAP(picked_node_bitmap);
		} else {
			bit_fmt(str, sizeof(str), picked_node_bitmap);
			debug2("Sequential pick using nodemap: %s",
			       str);
		}
	}

	return picked_node_bitmap;
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

/* select_p_state_save() in cons_common */

/* select_p_state_restore() in cons_common */

/* select_p_job_init() in cons_common */

/* select_p_node_init() in cons_common */

/*
 * select_p_job_test - Given a specification of scheduling requirements,
 *	identify the nodes which "best" satisfy the request.
 *	"best" is defined as either a minimal number of consecutive nodes
 *	or if sharing resources then sharing them with a job of similar size.
 * IN/OUT job_ptr - pointer to job being considered for initiation,
 *                  set's start_time when job expected to start
 * IN/OUT node_bitmap - usable nodes are set on input, nodes not required to
 *			satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * IN req_nodes - requested (or desired) count of nodes
 * IN mode - SELECT_MODE_RUN_NOW   (0): try to schedule job now
 *           SELECT_MODE_TEST_ONLY (1): test if job can ever run
 *           SELECT_MODE_WILL_RUN  (2): determine when and where job can run
 * IN preemptee_candidates - List of pointers to jobs which can be preempted.
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode==SELECT_MODE_TEST_ONLY or input pointer is NULL.
 * IN exc_core_bitmap - Cores to be excluded for use (in advanced reservation)
 * RET zero on success, EINVAL otherwise
 */
extern int select_p_job_test(job_record_t *job_ptr, bitstr_t *node_bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list,
			     bitstr_t *exc_core_bitmap)
{
	int rc;
	bitstr_t **exc_cores;

	xassert(node_bitmap);
	debug2("evaluating %pJ", job_ptr);
	if (!job_ptr->details)
		return EINVAL;

	/*
	 * FIXME: exc_core_bitmap is a full-system core bitmap to be replaced
	 * with a set of per-node bitmaps in a future release of Slurm
	 */
	exc_cores = core_bitmap_to_array(exc_core_bitmap);
#if _DEBUG
	if (exc_cores) {
		int i;
		char tmp[128];
		for (i = 0; i < node_record_count; i++) {
			if (!exc_cores[i])
				continue;
			bit_fmt(tmp, sizeof(tmp), exc_cores[i]);
			error("IN exc_cores[%d] %s", i, tmp);
		}
	}
#endif

	rc = common_job_test(job_ptr, node_bitmap, min_nodes, max_nodes,
			     req_nodes, mode, preemptee_candidates,
			     preemptee_job_list, exc_cores);

	free_core_array(&exc_cores);

	return rc;
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

/* select_p_update_node_config() in cons_common */

/* select_p_reconfigure() in cons_common */

/* select_p_resv_test() in cons_common */
