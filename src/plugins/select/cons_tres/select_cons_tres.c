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
const char *plugin_type = "select/cons_tres";
const uint32_t plugin_id      = SELECT_PLUGIN_CONS_TRES;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const uint32_t pstate_version = 7;	/* version control on saved state */
const uint16_t nodeinfo_magic = 0x8a5d;

/* Global variables */
bitstr_t **spec_core_res	= NULL;

/* Global functions */
extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(void);
extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo);

/* Local functions */
static bitstr_t *_array_to_core_bitmap(bitstr_t **core_res);
static bitstr_t **_core_bitmap_to_array(bitstr_t *core_bitmap);
static bitstr_t *_pick_first_cores(bitstr_t *avail_node_bitmap,
				   uint32_t node_cnt, uint32_t *core_cnt,
				   bitstr_t ***exc_cores);
static bitstr_t *_sequential_pick(bitstr_t *avail_node_bitmap,
				  uint32_t node_cnt, uint32_t *core_cnt,
				  bitstr_t ***exc_cores);
static void _spec_core_filter(bitstr_t **avail_cores);

/* Translate system-wide core bitmap to per-node core bitmap array */
static bitstr_t **_core_bitmap_to_array(bitstr_t *core_bitmap)
{
	bitstr_t **core_array = NULL;
	int i, i_first, i_last, j, c;
	int node_inx, last_node_inx = 0, core_offset;
	char tmp[128];

	if (!core_bitmap)
		return core_array;

#if _DEBUG
	bit_fmt(tmp, sizeof(tmp), core_bitmap);
	error("%s: %s: IN core bitmap %s", plugin_type, __func__, tmp);
#endif

	i_first = bit_ffs(core_bitmap);
	if (i_first == -1)
		return core_array;
	i_last = bit_fls(core_bitmap);
	core_array = build_core_array();
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(core_bitmap, i))
			continue;
		for (j = last_node_inx; j < select_node_cnt; j++) {
			if (i < select_node_record[j].cume_cores) {
				node_inx = j;
				break;
			}
		}
		if (j >= select_node_cnt) {
			bit_fmt(tmp, sizeof(tmp), core_bitmap);
			error("%s: %s: error translating core bitmap %s",
			      plugin_type, __func__, tmp);
			break;
		}
		/* Copy all core bitmaps for this node here */
		core_array[node_inx] =
			bit_alloc(select_node_record[node_inx].tot_cores);
		core_offset = select_node_record[node_inx].cume_cores -
			      select_node_record[node_inx].tot_cores;
		for (c = 0; c < select_node_record[node_inx].tot_cores; c++) {
			if (bit_test(core_bitmap, core_offset + c))
				bit_set(core_array[node_inx], c);
		}
	}

#if _DEBUG
	for (i = 0; i < select_node_cnt; i++) {
		if (!core_array[i])
			continue;
		bit_fmt(tmp, sizeof(tmp), core_array[i]);
		error("%s: %s: OUT core bitmap[%d] %s", plugin_type, __func__,
		      i, tmp);
	}
#endif

	return core_array;
}

/* Translate per-node core bitmap array to system-wide core bitmap */
static bitstr_t *_array_to_core_bitmap(bitstr_t **core_array)
{
	bitstr_t *core_bitmap = NULL;
	int i;
	int c, core_offset;
#if _DEBUG
	char tmp[128];
#endif

	if (!core_array)
		return core_bitmap;

#if _DEBUG
	for (i = 0; i < select_node_cnt; i++) {
		if (!core_array[i])
			continue;
		bit_fmt(tmp, sizeof(tmp), core_array[i]);
		error("%s: %s: OUT core bitmap[%d] %s", plugin_type, __func__,
		      i, tmp);
	}
#endif

	core_bitmap =
		bit_alloc(select_node_record[select_node_cnt-1].cume_cores);
	for (i = 0; i < select_node_cnt; i++) {
		if (!core_array[i])
			continue;
		core_offset = select_node_record[i].cume_cores -
			      select_node_record[i].tot_cores;
		for (c = 0; c < select_node_record[i].tot_cores; c++) {
			if (bit_test(core_array[i], c))
				bit_set(core_bitmap, core_offset + c);
		}
	}

#if _DEBUG
	bit_fmt(tmp, sizeof(tmp), core_bitmap);
	error("%s: %s: IN core bitmap %s", plugin_type, __func__, tmp);
#endif

	return core_bitmap;
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
#if _DEBUG
	char tmp[128];
	bitstr_t **tmp_cores;
#endif
	bitstr_t **avail_cores, **local_cores = NULL;
	bitstr_t *picked_node_bitmap = NULL;
	bitstr_t *tmp_core_bitmap;
	int c, c_cnt, i;
	int local_node_offset = 0;
	bool fini = false;

	if (!core_cnt || (core_cnt[0] == 0))
		return picked_node_bitmap;

	if (*exc_cores == NULL) {	/* Exclude no cores by default */
#if _DEBUG
		bit_fmt(tmp, sizeof(tmp), avail_node_bitmap);
		info("%s: avail_nodes:%s", __func__, tmp);
		info("%s: exc_cores: NULL", __func__);
#endif
		c = select_node_record[select_node_cnt-1].cume_cores;
		tmp_core_bitmap = bit_alloc(c);
		bit_not(tmp_core_bitmap);
		avail_cores = _core_bitmap_to_array(tmp_core_bitmap);
		local_cores = avail_cores;
		FREE_NULL_BITMAP(tmp_core_bitmap);
	} else {
#if _DEBUG
		tmp_cores = *exc_cores;
		bit_fmt(tmp, sizeof(tmp), avail_node_bitmap);
		info("%s: avail_nodes:%s", __func__, tmp);
		for (i = 0; i < select_node_cnt; i++) {
			if (!tmp_cores[i])
				continue;
			bit_fmt(tmp, sizeof(tmp), tmp_cores[i]);
			info("%s: exc_cores[%d]: %s", __func__, i, tmp);
		}
#endif
		/*
		 * Ensure all nodes in avail_node_bitmap are represented
		 * in exc_cores. For now include ALL nodes.
		 */
		c = select_node_record[select_node_cnt-1].cume_cores;
		tmp_core_bitmap = bit_alloc(c);
		bit_not(tmp_core_bitmap);
		avail_cores = _core_bitmap_to_array(tmp_core_bitmap);
		FREE_NULL_BITMAP(tmp_core_bitmap);
		core_array_and_not(avail_cores, *exc_cores);
	}

	_spec_core_filter(avail_cores);

	picked_node_bitmap = bit_alloc(select_node_cnt);
	for (i = 0; i < node_record_count; i++) {
		if (fini ||
		    !avail_cores[i] ||
		    !bit_test(avail_node_bitmap, i) ||
		    (bit_set_count(avail_cores[i]) <
		     core_cnt[local_node_offset])) {
			FREE_NULL_BITMAP(avail_cores[i]);
			continue;
		}
		bit_set(picked_node_bitmap, i);
		c_cnt = 0;
		for (c = 0; c < select_node_record[i].tot_cores; c++) {
			if (!bit_test(avail_cores[i], c))
				continue;
			if (++c_cnt > core_cnt[local_node_offset])
				bit_clear(avail_cores[i], c);
		}
		if (core_cnt[++local_node_offset] == 0)
			fini = true;
	}

	if (!fini) {
		info("%s: %s: reservation request can not be satisfied",
		     plugin_type, __func__);
		FREE_NULL_BITMAP(picked_node_bitmap);
		free_core_array(&local_cores);
	} else {
		*exc_cores = avail_cores;
#if _DEBUG
		for (i = 0; i < select_node_cnt; i++) {
			if (!avail_cores[i])
				continue;
			bit_fmt(tmp, sizeof(tmp), avail_cores[i]);
			error("%s: selected cores[%d] %s", __func__, i, tmp);
		}
#endif
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
	bitstr_t **avail_cores, **local_cores = NULL;
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
		info("%s: %s: Reservations requires %d cores (%u each on %u nodes, plus %d)",
		     plugin_type, __func__, total_core_cnt, cores_per_node,
		     node_cnt, extra_cores_needed);
	} else if (single_core_cnt) {
		info("%s: %s: Reservations requires %d cores total",
		     plugin_type, __func__, total_core_cnt);
	} else if (core_cnt && core_cnt[0]) {
		info("%s: %s: Reservations requires %d cores with %d cores on first node",
		     plugin_type, __func__, total_core_cnt, core_cnt[0]);
	} else {
		info("%s: %s: Reservations requires %u nodes total",
		     plugin_type, __func__, node_cnt);
	}
#endif

	picked_node_bitmap = bit_alloc(select_node_cnt);
	if (core_cnt) { /* Reservation is using partial nodes */
		debug2("%s: %s: Reservation is using partial nodes",
		       plugin_type, __func__);
		if (*exc_cores == NULL) {      /* Exclude no cores by default */
#if _DEBUG
			bit_fmt(tmp, sizeof(tmp), avail_node_bitmap);
			info("%s: avail_nodes:%s", __func__, tmp);
			info("%s: exc_cores: NULL", __func__);
#endif
			c = select_node_record[select_node_cnt-1].cume_cores;
			tmp_core_bitmap = bit_alloc(c);
			bit_not(tmp_core_bitmap);
			avail_cores = _core_bitmap_to_array(tmp_core_bitmap);
			local_cores = avail_cores;
			FREE_NULL_BITMAP(tmp_core_bitmap);
		} else {
#if _DEBUG
			tmp_cores = *exc_cores;
			bit_fmt(tmp, sizeof(tmp), avail_node_bitmap);
			info("%s: avail_nodes:%s", __func__, tmp);
			for (i = 0; i < select_node_cnt; i++) {
				if (!tmp_cores[i])
					continue;
				bit_fmt(tmp, sizeof(tmp), tmp_cores[i]);
				info("%s: exc_cores[%d]: %s", __func__, i, tmp);
			}
#endif
			/*
			 * Ensure all nodes in avail_node_bitmap are represented
			 * in exc_cores. For now include ALL nodes.
			 */
			c = select_node_record[select_node_cnt-1].cume_cores;
			tmp_core_bitmap = bit_alloc(c);
			bit_not(tmp_core_bitmap);
			avail_cores = _core_bitmap_to_array(tmp_core_bitmap);
			FREE_NULL_BITMAP(tmp_core_bitmap);
			core_array_and_not(avail_cores, *exc_cores);
		}
		_spec_core_filter(avail_cores);

		for (i = 0; i < select_node_cnt; i++) {
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
			for (c = 0; c < select_node_record[i].tot_cores; c++) {
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
			info("%s: %s: reservation request can not be satisfied",
			     plugin_type, __func__);
			FREE_NULL_BITMAP(picked_node_bitmap);
			free_core_array(&local_cores);
		} else {
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
			info("%s: %s: Reservation request can not be satisfied",
			     plugin_type, __func__);
			FREE_NULL_BITMAP(picked_node_bitmap);
		} else {
			bit_fmt(str, sizeof(str), picked_node_bitmap);
			debug2("%s: %s: Sequential pick using nodemap: %s",
			       plugin_type, __func__, str);
		}
	}

	return picked_node_bitmap;
}

/* Clear from avail_cores all specialized cores */
static void _spec_core_filter(bitstr_t **avail_cores)
{
	if (!spec_core_res)
		return;	/* No specialized cores */

	xassert(avail_cores);
	core_array_and_not(avail_cores, spec_core_res);
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
	cons_common_callbacks.mark_avail_cores = mark_avail_cores;
	cons_common_callbacks.dist_tasks_compute_c_b = dist_tasks_compute_c_b;

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	common_fini();

	free_core_array(&spec_core_res);

	return SLURM_SUCCESS;
}

/* select_p_state_save() in cons_common */

/* select_p_state_restore() in cons_common */

/* select_p_job_init() in cons_common */

/* select_p_node_ranking() in cons_common */

/* select_p_node_init() in cons_common */

/* select_p_block_init() in cons_common */

/*
 * select_p_job_test - Given a specification of scheduling requirements,
 *	identify the nodes which "best" satisfy the request.
 * 	"best" is defined as either a minimal number of consecutive nodes
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
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t *node_bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list,
			     bitstr_t *exc_core_bitmap)
{
	int rc;
	bitstr_t **exc_cores;

	xassert(node_bitmap);
	debug2("%s: %s: evaluating %pJ", plugin_type, __func__, job_ptr);
	if (!job_ptr->details)
		return EINVAL;

	/*
	 * FIXME: exc_core_bitmap is a full-system core bitmap to be replaced
	 * with a set of per-node bitmaps in a future release of Slurm
	 */
	exc_cores = _core_bitmap_to_array(exc_core_bitmap);
#if _DEBUG
	if (exc_cores) {
		int i;
		char tmp[128];
		for (i = 0; i < select_node_cnt; i++) {
			if (!exc_cores[i])
				continue;
			bit_fmt(tmp, sizeof(tmp), exc_cores[i]);
			error("%s: %s: IN exc_cores[%d] %s", plugin_type,
			      __func__, i, tmp);
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

/* select_p_job_mem_confirm() in cons_common */

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

/* select_p_job_mem_confirm() in cons_common */

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

/* select_p_update_node_state() in cons_common */

/* select_p_reconfigure() in cons_common */
extern bitstr_t *select_p_resv_test(resv_desc_msg_t *resv_desc_ptr,
				    uint32_t node_cnt,
				    bitstr_t *avail_node_bitmap,
				    bitstr_t **core_bitmap)
{
	bitstr_t **switches_bitmap;		/* nodes on this switch */
	bitstr_t ***switches_core_bitmap;	/* cores on this switch */
	int       *switches_core_cnt;		/* total cores on switch */
	int       *switches_node_cnt;		/* total nodes on switch */
	int       *switches_required;		/* set if has required node */

	bitstr_t *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t *picked_node_bitmap;
	uint32_t *core_cnt, flags;
	bitstr_t **exc_core_bitmap = NULL, **picked_core_bitmap;
	int32_t prev_rem_cores, rem_cores = 0, rem_cores_save, rem_nodes;
	uint32_t cores_per_node = 1;	/* Minimum cores per node to consider */
	bool aggr_core_cnt = false, clear_core, sufficient;
	int c, i, i_first, i_last, j, k, n;
	int best_fit_inx, best_fit_nodes;
	int best_fit_location = 0, best_fit_sufficient;

	xassert(avail_node_bitmap);
	xassert(resv_desc_ptr);

	/*
	 * FIXME: core_bitmap is a full-system core bitmap to be replaced
	 * with a set of per-node bitmaps in a future release of Slurm
	 */
	if (core_bitmap)
		exc_core_bitmap = _core_bitmap_to_array(*core_bitmap);

	core_cnt = resv_desc_ptr->core_cnt;
	flags = resv_desc_ptr->flags;

	if ((flags & RESERVE_FLAG_FIRST_CORES) && core_cnt) {
		/* Reservation request with "Flags=first_cores CoreCnt=#" */
		avail_nodes_bitmap = _pick_first_cores(avail_node_bitmap,
						       node_cnt, core_cnt,
						       &exc_core_bitmap);
		if (avail_nodes_bitmap && core_bitmap && exc_core_bitmap) {
			FREE_NULL_BITMAP(*core_bitmap);
			*core_bitmap = _array_to_core_bitmap(exc_core_bitmap);
		}
		free_core_array(&exc_core_bitmap);
		return avail_nodes_bitmap;
	}

	/* When reservation includes a nodelist we use _sequential_pick code */
	if (!switch_record_cnt || !switch_record_table || !node_cnt)  {
		/* Reservation request with "Nodes=* [CoreCnt=#]" */
		avail_nodes_bitmap = _sequential_pick(avail_node_bitmap,
						      node_cnt, core_cnt,
						      &exc_core_bitmap);
		if (avail_nodes_bitmap && core_bitmap && exc_core_bitmap) {
			FREE_NULL_BITMAP(*core_bitmap);
			*core_bitmap = _array_to_core_bitmap(exc_core_bitmap);
		}
		free_core_array(&exc_core_bitmap);
		return avail_nodes_bitmap;
	}

	/* Use topology state information */
	if (bit_set_count(avail_node_bitmap) < node_cnt) {
		free_core_array(&exc_core_bitmap);
		return NULL;
	}

	if (core_cnt && spec_core_res) {
		if (!exc_core_bitmap)
			exc_core_bitmap = build_core_array();
		core_array_or(exc_core_bitmap, spec_core_res);
	}

	rem_nodes = node_cnt;
	if (core_cnt && core_cnt[1]) {	/* Array of core counts */
		for (j = 0; core_cnt[j]; j++) {
			rem_cores += core_cnt[j];
			if (j == 0)
				cores_per_node = core_cnt[j];
			else if (cores_per_node > core_cnt[j])
				cores_per_node = core_cnt[j];
		}
	} else if (core_cnt) {		/* Aggregate core count */
		rem_cores = core_cnt[0];
		cores_per_node = core_cnt[0] / MAX(node_cnt, 1);
		aggr_core_cnt = true;
	} else if (cr_node_num_cores)
		cores_per_node = cr_node_num_cores[0];
	else
		cores_per_node = 1;
	rem_cores_save = rem_cores;

	/*
	 * Construct a set of switch array entries,
	 * use the same indexes as switch_record_table in slurmctld
	 */
	switches_bitmap = xcalloc(switch_record_cnt, sizeof(bitstr_t *));
	switches_core_bitmap = xcalloc(switch_record_cnt, sizeof(bitstr_t **));
	switches_core_cnt = xcalloc(switch_record_cnt, sizeof(int));
	switches_node_cnt = xcalloc(switch_record_cnt, sizeof(int));
	switches_required = xcalloc(switch_record_cnt, sizeof(int));

	for (i = 0; i < switch_record_cnt; i++) {
		switches_bitmap[i] =
			bit_copy(switch_record_table[i].node_bitmap);
		bit_and(switches_bitmap[i], avail_node_bitmap);
		switches_node_cnt[i] = bit_set_count(switches_bitmap[i]);
		switches_core_bitmap[i] = mark_avail_cores(switches_bitmap[i],
							   NO_VAL16);
		if (exc_core_bitmap) {
			core_array_and_not(switches_core_bitmap[i],
					   exc_core_bitmap);
		}
		switches_core_cnt[i] =
			count_core_array_set(switches_core_bitmap[i]);
		debug2("switch:%d nodes:%d cores:%d",
		       i, switches_node_cnt[i], switches_core_cnt[i]);
	}

	/* Remove nodes with fewer available cores than needed */
	if (core_cnt) {
		n = 0;
		for (j = 0; j < switch_record_cnt; j++) {
			i_first = bit_ffs(switches_bitmap[j]);
			if (i_first >= 0)
				i_last = bit_fls(switches_bitmap[j]);
			else
				i_last = i_first - 1;
			for (i = i_first; i <= i_last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				c = select_node_record[i].tot_cores;
				if (exc_core_bitmap && exc_core_bitmap[i])
					c -= bit_set_count(exc_core_bitmap[i]);
				clear_core = false;
				if (aggr_core_cnt && (c < cores_per_node)) {
					clear_core = true;
				} else if (aggr_core_cnt) {
					;
				} else if (c < core_cnt[n]) {
					clear_core = true;
				} else if (core_cnt[n]) {
					n++;
				}
				if (!clear_core)
					continue;
				for (k = 0; k < switch_record_cnt; k++) {
					if (!switches_bitmap[k] ||
					    !bit_test(switches_bitmap[k], i))
						continue;
					bit_clear(switches_bitmap[k], i);
					switches_node_cnt[k]--;
					switches_core_cnt[k] -= c;
				}
			}
		}
	}

#if SELECT_DEBUG
	/* Don't compile this, it slows things down too much */
	for (i = 0; i < switch_record_cnt; i++) {
		char *node_names = NULL;
		if (switches_node_cnt[i])
			node_names = bitmap2node_name(switches_bitmap[i]);
		info("switch=%s nodes=%u:%s cores:%d required:%u speed=%u",
		     switch_record_table[i].name,
		     switches_node_cnt[i], node_names,
		     switches_core_cnt[i], switches_required[i],
		     switch_record_table[i].link_speed);
		xfree(node_names);
	}
#endif

	/* Determine lowest level switch satisfying request with best fit */
	best_fit_inx = -1;
	for (j = 0; j < switch_record_cnt; j++) {
		if ((switches_node_cnt[j] < rem_nodes) ||
		    (core_cnt && (switches_core_cnt[j] < rem_cores)))
			continue;
		if ((best_fit_inx == -1) ||
		    (switch_record_table[j].level <
		     switch_record_table[best_fit_inx].level) ||
		    ((switch_record_table[j].level ==
		      switch_record_table[best_fit_inx].level) &&
		     (switches_node_cnt[j] < switches_node_cnt[best_fit_inx])))
			/* We should use core count by switch here as well */
			best_fit_inx = j;
	}
	if (best_fit_inx == -1) {
		debug("%s: could not find resources for reservation", __func__);
		goto fini;
	}

	/* Identify usable leafs (within higher switch having best fit) */
	for (j = 0; j < switch_record_cnt; j++) {
		if ((switch_record_table[j].level != 0) ||
		    (!bit_super_set(switches_bitmap[j],
				    switches_bitmap[best_fit_inx]))) {
			switches_node_cnt[j] = 0;
		}
	}

	/* Select resources from these leafs on a best-fit basis */
	avail_nodes_bitmap = bit_alloc(node_record_count);
	while (rem_nodes > 0) {
		best_fit_nodes = best_fit_sufficient = 0;
		for (j = 0; j < switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			if (core_cnt) {
				sufficient =
					(switches_node_cnt[j] >= rem_nodes) &&
					(switches_core_cnt[j] >= rem_cores);
			} else
				sufficient = switches_node_cnt[j] >= rem_nodes;
			/*
			 * If first possibility OR
			 * first set large enough for request OR
			 * tightest fit (less resource waste) OR
			 * nothing yet large enough, but this is biggest
			 */
			if ((best_fit_nodes == 0) ||
			    (sufficient && (best_fit_sufficient == 0)) ||
			    (sufficient &&
			     (switches_node_cnt[j] < best_fit_nodes)) ||
			    ((sufficient == 0) &&
			     (switches_node_cnt[j] > best_fit_nodes))) {
				best_fit_nodes = switches_node_cnt[j];
				best_fit_location = j;
				best_fit_sufficient = sufficient;
			}
		}
		if (best_fit_nodes == 0)
			break;
		/* Use select nodes from this leaf */
		i_first = bit_ffs(switches_bitmap[best_fit_location]);
		if (i_first >= 0)
			i_last = bit_fls(switches_bitmap[best_fit_location]);
		else
			i_last = i_first - 1;
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(switches_bitmap[best_fit_location], i))
				continue;
			bit_clear(switches_bitmap[best_fit_location], i);
			switches_node_cnt[best_fit_location]--;

			if (bit_test(avail_nodes_bitmap, i)) {
				/*
				 * node on multiple leaf switches
				 * and already selected
				 */
				continue;
			}
			c = select_node_record[i].tot_cores;
			if (exc_core_bitmap && exc_core_bitmap[i])
				c -= bit_set_count(exc_core_bitmap[i]);
			if (c < cores_per_node)
				continue;
			debug2("Using node %d with %d cores available", i, c);
			bit_set(avail_nodes_bitmap, i);
			rem_cores -= c;
			if (--rem_nodes <= 0)
				break;
		}
		switches_node_cnt[best_fit_location] = 0;
	}

	if ((rem_nodes > 0) || (rem_cores > 0))	/* insufficient resources */
		FREE_NULL_BITMAP(avail_nodes_bitmap);

fini:	for (i = 0; i < switch_record_cnt; i++) {
		FREE_NULL_BITMAP(switches_bitmap[i]);
		free_core_array(&switches_core_bitmap[i]);
	}
	xfree(switches_bitmap);
	xfree(switches_core_bitmap);
	xfree(switches_core_cnt);
	xfree(switches_node_cnt);
	xfree(switches_required);

	if (avail_nodes_bitmap && core_cnt) {
		/* Reservation is using partial nodes */
		picked_node_bitmap = bit_alloc(bit_size(avail_node_bitmap));
		picked_core_bitmap = build_core_array();

		rem_cores = rem_cores_save;
		n = 0;
		prev_rem_cores = -1;
		while (rem_cores) {
			int avail_cores_in_node, inx, i;

			inx = bit_ffs(avail_nodes_bitmap);
			if ((inx < 0) && aggr_core_cnt && (rem_cores > 0) &&
			    (rem_cores != prev_rem_cores)) {
				/*
				 * Make another pass over nodes to reach
				 * requested aggregate core count
				 */
				bit_or(avail_nodes_bitmap, picked_node_bitmap);
				inx = bit_ffs(avail_nodes_bitmap);
				prev_rem_cores = rem_cores;
				cores_per_node = 1;
			}
			if (inx < 0)
				break;

			debug2("Using node inx:%d cores_per_node:%d rem_cores:%u",
			       inx, cores_per_node, rem_cores);

			/* Clear this node from the initial available bitmap */
			bit_clear(avail_nodes_bitmap, inx);

			if (cr_node_num_cores[inx] < cores_per_node)
				continue;

			avail_cores_in_node = select_node_record[inx].tot_cores;
			if (exc_core_bitmap && exc_core_bitmap[inx]) {
				avail_cores_in_node -=
					bit_set_count(exc_core_bitmap[inx]);
			}
			debug2("Node inx:%d has %d available cores", inx,
			       avail_cores_in_node);
			if (avail_cores_in_node < cores_per_node)
				continue;

			avail_cores_in_node = 0;
			if (!picked_core_bitmap[inx]) {
				picked_core_bitmap[inx] =
					bit_alloc(cr_node_num_cores[inx]);
			}
			for (i = 0; i < cr_node_num_cores[inx]; i++) {
				if ((!exc_core_bitmap ||
				     !exc_core_bitmap[inx] ||
				     !bit_test(exc_core_bitmap[inx], i)) &&
				    !bit_test(picked_core_bitmap[inx], i)) {
					bit_set(picked_core_bitmap[inx], i);
					rem_cores--;
					avail_cores_in_node++;
				}
				if (rem_cores == 0)
					break;
				if (aggr_core_cnt &&
				    (avail_cores_in_node >= cores_per_node))
					break;
				if (!aggr_core_cnt &&
				    (avail_cores_in_node >= core_cnt[n]))
					break;
			}

			/* Add this node to the final node bitmap */
			if (avail_cores_in_node)
				bit_set(picked_node_bitmap, inx);
			n++;
		}
		FREE_NULL_BITMAP(avail_nodes_bitmap);
		free_core_array(&exc_core_bitmap);

		if (rem_cores) {
			info("reservation request can not be satisfied");
			FREE_NULL_BITMAP(picked_node_bitmap);
			picked_node_bitmap = NULL;
		} else {
			*core_bitmap =
				_array_to_core_bitmap(picked_core_bitmap);
		}
		free_core_array(&picked_core_bitmap);
		return picked_node_bitmap;
	}
	free_core_array(&exc_core_bitmap);

	return avail_nodes_bitmap;
}
