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

	cons_common_callbacks.add_job_to_res = add_job_to_res;
	cons_common_callbacks.can_job_fit_in_row = can_job_fit_in_row;
	cons_common_callbacks.can_job_run_on_node = can_job_run_on_node;
	cons_common_callbacks.choose_nodes = choose_nodes;
	cons_common_callbacks.verify_node_state = verify_node_state;
	cons_common_callbacks.mark_avail_cores = mark_avail_cores;
	cons_common_callbacks.build_row_bitmaps = build_row_bitmaps;
	cons_common_callbacks.dist_tasks_compute_c_b = dist_tasks_compute_c_b;

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	common_fini();

	free_core_array(&spec_core_res);

	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_state_save(char *dir_name)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_state_restore(char *dir_name)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_job_init(List job_list)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern bool select_p_node_ranking(struct node_record *node_ptr, int node_cnt)
{
	return false;
}

/* This is Part 1 of a 4-part procedure which can be found in
 * src/slurmctld/read_config.c. The whole story goes like this:
 *
 * Step 1: select_g_node_init          : initializes the global node arrays
 * Step 2: select_g_state_restore      : NO-OP - nothing to restore
 * Step 3: select_g_job_init           : NO-OP - nothing to initialize
 * Step 4: select_g_select_nodeinfo_set: called from reset_job_bitmaps() with
 *                                       each valid recovered job_ptr AND from
 *                                       select_nodes(), this procedure adds
 *                                       job data to the 'select_part_record'
 *                                       global array
 */
extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
	return common_node_init(node_ptr, node_cnt);
}

/* Unused for this plugin */
extern int select_p_block_init(List part_list)
{
	return SLURM_SUCCESS;
}

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

/* Unused for this plugin */
extern int select_p_job_begin(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

/* Determine if allocated nodes are usable (powered up) */
extern int select_p_job_ready(struct job_record *job_ptr)
{
	int i, i_first, i_last;
	struct node_record *node_ptr;

	if (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr)) {
		/* Gang scheduling might suspend job immediately */
		return 0;
	}

	if ((job_ptr->node_bitmap == NULL) ||
	    ((i_first = bit_ffs(job_ptr->node_bitmap)) == -1))
		return READY_NODE_STATE;
	i_last  = bit_fls(job_ptr->node_bitmap);
	for (i = i_first; i <= i_last; i++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		node_ptr = node_record_table_ptr + i;
		if (IS_NODE_POWER_SAVE(node_ptr) || IS_NODE_POWER_UP(node_ptr))
			return 0;
	}

	return READY_NODE_STATE;
}

static void _dump_job_res(struct job_resources *job)
{
	char str[64];

	if (job->core_bitmap)
		bit_fmt(str, sizeof(str), job->core_bitmap);
	else
		sprintf(str, "[no core_bitmap]");
	info("DEBUG: Dump job_resources: nhosts %u core_bitmap %s",
	     job->nhosts, str);
}

extern int select_p_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	struct part_res_record *part_record_ptr = select_part_record;
	struct node_use_record *node_usage = select_node_usage;
	struct job_resources *job = job_ptr->job_resrcs;
	struct part_res_record *p_ptr;
	int i, i_first, i_last, node_inx, n;
	List gres_list;
	bool old_job = false;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
	if (!job || !job->core_bitmap) {
		error("%s: %s: %pJ has no job_resrcs info",
		      plugin_type, __func__, job_ptr);
		return SLURM_ERROR;
	}

	debug3("%s: %s: %pJ node %s",
	       plugin_type, __func__, job_ptr, node_ptr->name);
	if (job_ptr->start_time < slurmctld_config.boot_time)
		old_job = true;
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		_dump_job_res(job);

	/* subtract memory */
	node_inx  = node_ptr - node_record_table_ptr;
	i_first = bit_ffs(job->node_bitmap);
	if (i_first >= 0)
		i_last  = bit_fls(job->node_bitmap);
	else
		i_last = i_first - 1;
	for (i = i_first, n = 0; i <= i_last; i++) {
		if (!bit_test(job->node_bitmap, i))
			continue;
		if (i != node_inx) {
			n++;
			continue;
		}

		if (job->cpus[n] == 0) {
			info("%s: %s: attempt to remove node %s from %pJ again",
			     plugin_type, __func__, node_ptr->name, job_ptr);
			return SLURM_SUCCESS;
		}

		if (node_usage[i].gres_list)
			gres_list = node_usage[i].gres_list;
		else
			gres_list = node_ptr->gres_list;
		gres_plugin_job_dealloc(job_ptr->gres_list, gres_list, n,
					job_ptr->job_id, node_ptr->name,
					old_job, job_ptr->user_id, true);
		gres_plugin_node_state_log(gres_list, node_ptr->name);

		if (node_usage[i].alloc_memory < job->memory_allocated[n]) {
			error("%s: %s: node %s memory is underallocated "
			      "(%"PRIu64"-%"PRIu64") for %pJ", plugin_type,
			      __func__, node_ptr->name,
			      node_usage[i].alloc_memory,
			      job->memory_allocated[n], job_ptr);
			node_usage[i].alloc_memory = 0;
		} else
			node_usage[i].alloc_memory -= job->memory_allocated[n];

		extract_job_resources_node(job, n);

		break;
	}

	if (IS_JOB_SUSPENDED(job_ptr))
		return SLURM_SUCCESS;	/* No cores allocated to the job now */

	/* subtract cores, reconstruct rows with remaining jobs */
	if (!job_ptr->part_ptr) {
		error("%s: %s: removed %pJ does not have a partition assigned",
		      plugin_type, __func__, job_ptr);
		return SLURM_ERROR;
	}

	for (p_ptr = part_record_ptr; p_ptr; p_ptr = p_ptr->next) {
		if (p_ptr->part_ptr == job_ptr->part_ptr)
			break;
	}
	if (!p_ptr) {
		error("%s: %s: removed %pJ could not find part %s",
		      plugin_type, __func__, job_ptr, job_ptr->part_ptr->name);
		return SLURM_ERROR;
	}

	if (!p_ptr->row)
		return SLURM_SUCCESS;

	/* look for the job in the partition's job_list */
	n = 0;
	for (i = 0; i < p_ptr->num_rows; i++) {
		uint32_t j;
		for (j = 0; j < p_ptr->row[i].num_jobs; j++) {
			if (p_ptr->row[i].job_list[j] != job)
				continue;
			debug3("%s: %s: found %pJ in part %s row %u",
			       plugin_type, __func__, job_ptr,
			       p_ptr->part_ptr->name, i);
			/* found job - we're done, don't actually remove */
			n = 1;
			i = p_ptr->num_rows;
			break;
		}
	}
	if (n == 0) {
		error("%s: %s: could not find %pJ in partition %s",
		      plugin_type, __func__, job_ptr, p_ptr->part_ptr->name);
		return SLURM_ERROR;
	}


	/* some node of job removed from core-bitmap, so rebuild core bitmaps */
	build_row_bitmaps(p_ptr, NULL);

	/*
	 * Adjust the node_state of the node removed from this job.
	 * If all cores are now available, set node_state = NODE_CR_AVAILABLE
	 */
	if (node_usage[node_inx].node_state >= job->node_req) {
		node_usage[node_inx].node_state -= job->node_req;
	} else {
		error("%s: %s: node_state miscount", plugin_type, __func__);
		node_usage[node_inx].node_state = NODE_CR_AVAILABLE;
	}

	return SLURM_SUCCESS;
}

static job_resources_t *_create_job_resources(int node_cnt)
{
	job_resources_t *job_resrcs_ptr;

	job_resrcs_ptr = create_job_resources();
	job_resrcs_ptr->cpu_array_reps = xcalloc(node_cnt, sizeof(uint32_t));
	job_resrcs_ptr->cpu_array_value = xcalloc(node_cnt, sizeof(uint16_t));
	job_resrcs_ptr->cpus = xcalloc(node_cnt, sizeof(uint16_t));
	job_resrcs_ptr->cpus_used = xcalloc(node_cnt, sizeof(uint16_t));
	job_resrcs_ptr->memory_allocated = xcalloc(node_cnt, sizeof(uint64_t));
	job_resrcs_ptr->memory_used = xcalloc(node_cnt, sizeof(uint64_t));
	job_resrcs_ptr->nhosts = node_cnt;
	return job_resrcs_ptr;
}

extern int select_p_job_expand(struct job_record *from_job_ptr,
			       struct job_record *to_job_ptr)
{
	job_resources_t *from_job_resrcs_ptr, *to_job_resrcs_ptr,
		        *new_job_resrcs_ptr;
	struct node_record *node_ptr;
	int first_bit, last_bit, i, node_cnt;
	bool from_node_used, to_node_used;
	int from_node_offset, to_node_offset, new_node_offset;
	bitstr_t *tmp_bitmap, *tmp_bitmap2;

	xassert(from_job_ptr);
	xassert(from_job_ptr->details);
	xassert(to_job_ptr);
	xassert(to_job_ptr->details);

	if (from_job_ptr->job_id == to_job_ptr->job_id) {
		error("%s: %s: attempt to merge %pJ with self",
		      plugin_type, __func__, from_job_ptr);
		return SLURM_ERROR;
	}

	from_job_resrcs_ptr = from_job_ptr->job_resrcs;
	if ((from_job_resrcs_ptr == NULL) ||
	    (from_job_resrcs_ptr->cpus == NULL) ||
	    (from_job_resrcs_ptr->core_bitmap == NULL) ||
	    (from_job_resrcs_ptr->node_bitmap == NULL)) {
		error("%s: %s: %pJ lacks a job_resources struct",
		      plugin_type, __func__, from_job_ptr);
		return SLURM_ERROR;
	}
	to_job_resrcs_ptr = to_job_ptr->job_resrcs;
	if ((to_job_resrcs_ptr == NULL) ||
	    (to_job_resrcs_ptr->cpus == NULL) ||
	    (to_job_resrcs_ptr->core_bitmap == NULL) ||
	    (to_job_resrcs_ptr->node_bitmap == NULL)) {
		error("%s: %s: %pJ lacks a job_resources struct",
		      plugin_type, __func__, to_job_ptr);
		return SLURM_ERROR;
	}

	if (to_job_ptr->gres_list) {	/* Can't reset gres/mps fields today */
		error("%s: %s: %pJ has allocated GRES",
		      plugin_type, __func__, to_job_ptr);
		return SLURM_ERROR;
	}
	if (from_job_ptr->gres_list) {	/* Can't reset gres/mps fields today */
		error("%s: %s: %pJ has allocated GRES",
		      plugin_type, __func__, from_job_ptr);
		return SLURM_ERROR;
	}

	(void) common_rm_job_res(select_part_record, select_node_usage, from_job_ptr,
			  0, true);
	(void) common_rm_job_res(select_part_record, select_node_usage, to_job_ptr, 0,
			  true);

	if (to_job_resrcs_ptr->core_bitmap_used) {
		i = bit_size(to_job_resrcs_ptr->core_bitmap_used);
		bit_nclear(to_job_resrcs_ptr->core_bitmap_used, 0, i-1);
	}

	tmp_bitmap = bit_copy(to_job_resrcs_ptr->node_bitmap);
	bit_or(tmp_bitmap, from_job_resrcs_ptr->node_bitmap);
	tmp_bitmap2 = bit_copy(to_job_ptr->node_bitmap);
	bit_or(tmp_bitmap2, from_job_ptr->node_bitmap);
	bit_and(tmp_bitmap, tmp_bitmap2);
	bit_free(tmp_bitmap2);
	node_cnt = bit_set_count(tmp_bitmap);

	new_job_resrcs_ptr = _create_job_resources(node_cnt);
	new_job_resrcs_ptr->ncpus = from_job_resrcs_ptr->ncpus +
				    to_job_resrcs_ptr->ncpus;
	new_job_resrcs_ptr->node_req = to_job_resrcs_ptr->node_req;
	new_job_resrcs_ptr->node_bitmap = tmp_bitmap;
	new_job_resrcs_ptr->nodes = bitmap2node_name(new_job_resrcs_ptr->
						     node_bitmap);
	new_job_resrcs_ptr->whole_node = to_job_resrcs_ptr->whole_node;
	build_job_resources(new_job_resrcs_ptr, node_record_table_ptr,
			    select_fast_schedule);
	xfree(to_job_ptr->node_addr);
	to_job_ptr->node_addr = xcalloc(node_cnt, sizeof(slurm_addr_t));
	to_job_ptr->total_cpus = 0;

	first_bit = MIN(bit_ffs(from_job_resrcs_ptr->node_bitmap),
			bit_ffs(to_job_resrcs_ptr->node_bitmap));
	last_bit =  MAX(bit_fls(from_job_resrcs_ptr->node_bitmap),
			bit_fls(to_job_resrcs_ptr->node_bitmap));
	from_node_offset = to_node_offset = new_node_offset = -1;
	for (i = first_bit; i <= last_bit; i++) {
		from_node_used = to_node_used = false;
		if (bit_test(from_job_resrcs_ptr->node_bitmap, i)) {
			from_node_used = bit_test(from_job_ptr->node_bitmap,i);
			from_node_offset++;
		}
		if (bit_test(to_job_resrcs_ptr->node_bitmap, i)) {
			to_node_used = bit_test(to_job_ptr->node_bitmap, i);
			to_node_offset++;
		}
		if (!from_node_used && !to_node_used)
			continue;
		new_node_offset++;
		node_ptr = node_record_table_ptr + i;
		memcpy(&to_job_ptr->node_addr[new_node_offset],
                       &node_ptr->slurm_addr, sizeof(slurm_addr_t));
		if (from_node_used) {
			/*
			 * Merge alloc info from both "from" and "to" jobs,
			 * leave "from" job with no allocated CPUs or memory
			 *
			 * The following fields should be zero:
			 * from_job_resrcs_ptr->cpus_used[from_node_offset]
			 * from_job_resrcs_ptr->memory_used[from_node_offset];
			 */
			new_job_resrcs_ptr->cpus[new_node_offset] =
				from_job_resrcs_ptr->cpus[from_node_offset];
			from_job_resrcs_ptr->cpus[from_node_offset] = 0;
			new_job_resrcs_ptr->memory_allocated[new_node_offset] =
				from_job_resrcs_ptr->
				memory_allocated[from_node_offset];
			job_resources_bits_copy(new_job_resrcs_ptr,
						new_node_offset,
						from_job_resrcs_ptr,
						from_node_offset);
		}
		if (to_node_used) {
			/*
			 * Merge alloc info from both "from" and "to" jobs
			 *
			 * DO NOT double count the allocated CPUs in partition
			 * with Shared nodes
			 */
			new_job_resrcs_ptr->cpus[new_node_offset] +=
				to_job_resrcs_ptr->cpus[to_node_offset];
			new_job_resrcs_ptr->cpus_used[new_node_offset] +=
				to_job_resrcs_ptr->cpus_used[to_node_offset];
			new_job_resrcs_ptr->memory_allocated[new_node_offset]+=
				to_job_resrcs_ptr->
				memory_allocated[to_node_offset];
			new_job_resrcs_ptr->memory_used[new_node_offset] +=
				to_job_resrcs_ptr->memory_used[to_node_offset];
			job_resources_bits_copy(new_job_resrcs_ptr,
						new_node_offset,
						to_job_resrcs_ptr,
						to_node_offset);
			if (from_node_used) {
				/* Adjust CPU count for shared CPUs */
				int from_core_cnt, to_core_cnt, new_core_cnt;
				from_core_cnt = count_job_resources_node(
							from_job_resrcs_ptr,
							from_node_offset);
				to_core_cnt = count_job_resources_node(
							to_job_resrcs_ptr,
							to_node_offset);
				new_core_cnt = count_job_resources_node(
							new_job_resrcs_ptr,
							new_node_offset);
				if ((from_core_cnt + to_core_cnt) !=
				    new_core_cnt) {
					new_job_resrcs_ptr->
						cpus[new_node_offset] *=
						new_core_cnt;
					new_job_resrcs_ptr->
						cpus[new_node_offset] /=
						(from_core_cnt + to_core_cnt);
				}
			}
		}
		if (to_job_ptr->details->whole_node == 1) {
			to_job_ptr->total_cpus += select_node_record[i].cpus;
		} else {
			to_job_ptr->total_cpus += new_job_resrcs_ptr->
						  cpus[new_node_offset];
		}
	}
	build_job_resources_cpu_array(new_job_resrcs_ptr);
	gres_plugin_job_merge(from_job_ptr->gres_list,
			      from_job_resrcs_ptr->node_bitmap,
			      to_job_ptr->gres_list,
			      to_job_resrcs_ptr->node_bitmap);

	/* Now swap data: "new" -> "to" and clear "from" */
	free_job_resources(&to_job_ptr->job_resrcs);
	to_job_ptr->job_resrcs = new_job_resrcs_ptr;

	to_job_ptr->cpu_cnt = to_job_ptr->total_cpus;
	to_job_ptr->details->min_cpus = to_job_ptr->total_cpus;
	to_job_ptr->details->max_cpus = to_job_ptr->total_cpus;
	from_job_ptr->total_cpus   = 0;
	from_job_resrcs_ptr->ncpus = 0;
	from_job_ptr->details->min_cpus = 0;
	from_job_ptr->details->max_cpus = 0;

	from_job_ptr->total_nodes   = 0;
	from_job_resrcs_ptr->nhosts = 0;
	from_job_ptr->node_cnt      = 0;
	from_job_ptr->details->min_nodes = 0;
	to_job_ptr->total_nodes     = new_job_resrcs_ptr->nhosts;
	to_job_ptr->node_cnt        = new_job_resrcs_ptr->nhosts;

	bit_or(to_job_ptr->node_bitmap, from_job_ptr->node_bitmap);
	bit_nclear(from_job_ptr->node_bitmap, 0, (node_record_count - 1));
	bit_nclear(from_job_resrcs_ptr->node_bitmap, 0,
		   (node_record_count - 1));

	xfree(to_job_ptr->nodes);
	to_job_ptr->nodes = xstrdup(new_job_resrcs_ptr->nodes);
	xfree(from_job_ptr->nodes);
	from_job_ptr->nodes = xstrdup("");
	xfree(from_job_resrcs_ptr->nodes);
	from_job_resrcs_ptr->nodes = xstrdup("");

	(void) common_add_job_to_res(to_job_ptr, 0);

	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_job_signal(struct job_record *job_ptr, int signal)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_mem_confirm(struct job_record *job_ptr)
{
	int i_first, i_last, i, offset;
	uint64_t avail_mem, lowest_mem = 0;

	xassert(job_ptr);

	if (((job_ptr->bit_flags & NODE_MEM_CALC) == 0) ||
	    (select_fast_schedule != 0))
		return SLURM_SUCCESS;
	if ((job_ptr->details == NULL) ||
	    (job_ptr->job_resrcs == NULL) ||
	    (job_ptr->job_resrcs->node_bitmap == NULL) ||
	    (job_ptr->job_resrcs->memory_allocated == NULL))
		return SLURM_ERROR;
	i_first = bit_ffs(job_ptr->job_resrcs->node_bitmap);
	if (i_first >= 0)
		i_last = bit_fls(job_ptr->job_resrcs->node_bitmap);
	else
		i_last = i_first - 1;
	for (i = i_first, offset = 0; i <= i_last; i++) {
		if (!bit_test(job_ptr->job_resrcs->node_bitmap, i))
			continue;
		avail_mem = select_node_record[i].real_memory -
			    select_node_record[i].mem_spec_limit;
		job_ptr->job_resrcs->memory_allocated[offset] = avail_mem;
		select_node_usage[i].alloc_memory = avail_mem;
		if ((offset == 0) || (lowest_mem > avail_mem))
			lowest_mem = avail_mem;
		offset++;
	}
	job_ptr->details->pn_min_memory = lowest_mem;

	return SLURM_SUCCESS;
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("%s: %s: %pJ", plugin_type, __func__, job_ptr);

	common_rm_job_res(select_part_record, select_node_usage, job_ptr, 0, true);

	return SLURM_SUCCESS;
}

/*
 * NOTE: This function is not called with gang scheduling because it
 * needs to track how many jobs are running or suspended on each node.
 * This sum is compared with the partition's Shared parameter
 */
extern int select_p_job_suspend(struct job_record *job_ptr, bool indf_susp)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		if (indf_susp)
			info("%s: %s: %pJ indf_susp", plugin_type, __func__,
			     job_ptr);
		else
			info("%s: %s: %pJ", plugin_type, __func__, job_ptr);
	}
	if (!indf_susp)
		return SLURM_SUCCESS;

	return common_rm_job_res(select_part_record, select_node_usage, job_ptr, 2,
			  false);
}

/* See NOTE with select_p_job_suspend() above */
extern int select_p_job_resume(struct job_record *job_ptr, bool indf_susp)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		if (indf_susp)
			info("%s: %s: %pJ indf_susp", plugin_type, __func__,
			     job_ptr);
		else
			info("%s: %s: %pJ", plugin_type, __func__, job_ptr);
	}
	if (!indf_susp)
		return SLURM_SUCCESS;

	return common_add_job_to_res(job_ptr, 2);
}


extern bitstr_t *select_p_step_pick_nodes(struct job_record *job_ptr,
					  select_jobinfo_t *jobinfo,
					  uint32_t node_count,
					  bitstr_t **avail_nodes)
{
	return NULL;
}

/* Unused for this plugin */
extern int select_p_step_start(struct step_record *step_ptr)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_step_finish(struct step_record *step_ptr, bool killing_step)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_pack(select_nodeinfo_t *nodeinfo,
					 Buf buffer,
					 uint16_t protocol_version)
{
	select_nodeinfo_t *nodeinfo_empty = NULL;

	if (!nodeinfo) {
		/*
		 * We should never get here,
		 * but avoid abort with bad data structures
		 */
		error("%s: nodeinfo is NULL", __func__);
		nodeinfo_empty = xmalloc(sizeof(select_nodeinfo_t));
		nodeinfo = nodeinfo_empty;
	}

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(nodeinfo->alloc_cpus, buffer);
		pack64(nodeinfo->alloc_memory, buffer);
		packstr(nodeinfo->tres_alloc_fmt_str, buffer);
		packdouble(nodeinfo->tres_alloc_weighted, buffer);
	}
	xfree(nodeinfo_empty);

	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo,
					   Buf buffer,
					   uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	select_nodeinfo_t *nodeinfo_ptr = NULL;

	nodeinfo_ptr = select_p_select_nodeinfo_alloc();
	*nodeinfo = nodeinfo_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&nodeinfo_ptr->alloc_cpus, buffer);
		safe_unpack64(&nodeinfo_ptr->alloc_memory, buffer);
		safe_unpackstr_xmalloc(&nodeinfo_ptr->tres_alloc_fmt_str,
				       &uint32_tmp, buffer);
		safe_unpackdouble(&nodeinfo_ptr->tres_alloc_weighted, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	error("%s: error unpacking here", __func__);
	select_p_select_nodeinfo_free(nodeinfo_ptr);
	*nodeinfo = NULL;

	return SLURM_ERROR;
}

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(void)
{
	select_nodeinfo_t *nodeinfo = xmalloc(sizeof(struct select_nodeinfo));

	nodeinfo->magic = NODEINFO_MAGIC;

	return nodeinfo;
}

extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
	if (nodeinfo) {
		if (nodeinfo->magic != NODEINFO_MAGIC) {
			error("%s: nodeinfo magic bad", __func__);
			return EINVAL;
		}
		xfree(nodeinfo->tres_alloc_cnt);
		xfree(nodeinfo->tres_alloc_fmt_str);
		xfree(nodeinfo);
	}
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set_all(void)
{
	static time_t last_set_all = 0;
	struct part_res_record *p_ptr;
	struct node_record *node_ptr = NULL;
	int i, n;
	uint32_t alloc_cpus, alloc_cores, node_cores, node_cpus, node_threads;
	uint32_t node_boards, node_sockets, total_node_cores;
	bitstr_t **alloc_core_bitmap = NULL;
	List gres_list;

	/*
	 * only set this once when the last_node_update is newer than
	 * the last time we set things up.
	 */
	if (last_set_all && (last_node_update < last_set_all)) {
		debug2("%s: Node data hasn't changed since %ld", __func__,
		       (long)last_set_all);
		return SLURM_NO_CHANGE_IN_DATA;
	}
	last_set_all = last_node_update;

	/*
	 * Build core bitmap array representing all cores allocated to all
	 * active jobs (running or preempted jobs)
	 */
	for (p_ptr = select_part_record; p_ptr; p_ptr = p_ptr->next) {
		if (!p_ptr->row)
			continue;
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!p_ptr->row[i].row_bitmap)
				continue;
			if (!alloc_core_bitmap) {
				alloc_core_bitmap =
				 	copy_core_array(
						p_ptr->row[i].row_bitmap);
			} else {
				core_array_or(alloc_core_bitmap,
					      p_ptr->row[i].row_bitmap);
			}
		}
	}

	for (n = 0, node_ptr = node_record_table_ptr;
	     n < select_node_cnt; n++, node_ptr++) {
		select_nodeinfo_t *nodeinfo = NULL;
		/*
		 * We have to use the '_g_' here to make sure we get the
		 * correct data to work on.  i.e. select/cray calls this plugin
		 * and has it's own struct.
		 */
		select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
					     SELECT_NODEDATA_PTR, 0,
					     (void *)&nodeinfo);
		if (!nodeinfo) {
			error("%s: no nodeinfo returned from structure",
			      __func__);
			continue;
		}

		if (slurmctld_conf.fast_schedule) {
			node_boards  = node_ptr->config_ptr->boards;
			node_sockets = node_ptr->config_ptr->sockets;
			node_cores   = node_ptr->config_ptr->cores;
			node_cpus    = node_ptr->config_ptr->cpus;
			node_threads = node_ptr->config_ptr->threads;
		} else {
			node_boards  = node_ptr->boards;
			node_sockets = node_ptr->sockets;
			node_cores   = node_ptr->cores;
			node_cpus    = node_ptr->cpus;
			node_threads = node_ptr->threads;
		}
		total_node_cores = node_boards * node_sockets * node_cores;

		if (alloc_core_bitmap && alloc_core_bitmap[n])
			alloc_cores = bit_set_count(alloc_core_bitmap[n]);
		else
			alloc_cores = 0;

		/*
		 * Administrator could resume suspended jobs and oversubscribe
		 * cores, avoid reporting more cores in use than configured
		 */
		if (alloc_cores > total_node_cores)
			alloc_cpus = total_node_cores;
		else
			alloc_cpus = alloc_cores;

		/*
		 * The minimum allocatable unit may a core, so scale by thread
		 * count up to the proper CPU count as needed
		 */
		if (total_node_cores < node_cpus)
			alloc_cpus *= node_threads;
		nodeinfo->alloc_cpus = alloc_cpus;

		if (select_node_record) {
			nodeinfo->alloc_memory =
				select_node_usage[n].alloc_memory;
		} else {
			nodeinfo->alloc_memory = 0;
		}

		/* Build allocated TRES info */
		if (!nodeinfo->tres_alloc_cnt)
			nodeinfo->tres_alloc_cnt = xcalloc(slurmctld_tres_cnt,
							   sizeof(uint64_t));
		nodeinfo->tres_alloc_cnt[TRES_ARRAY_CPU] = alloc_cpus;
		nodeinfo->tres_alloc_cnt[TRES_ARRAY_MEM] =
			nodeinfo->alloc_memory;
		if (select_node_usage[n].gres_list)
			gres_list = select_node_usage[n].gres_list;
		else
			gres_list = node_ptr->gres_list;
		gres_set_node_tres_cnt(gres_list, nodeinfo->tres_alloc_cnt,
				       false);

		xfree(nodeinfo->tres_alloc_fmt_str);
		nodeinfo->tres_alloc_fmt_str =
			assoc_mgr_make_tres_str_from_array(
						nodeinfo->tres_alloc_cnt,
						TRES_STR_CONVERT_UNITS, false);
		nodeinfo->tres_alloc_weighted =
			assoc_mgr_tres_weighted(nodeinfo->tres_alloc_cnt,
					node_ptr->config_ptr->tres_weights,
					priority_flags, false);
	}
	free_core_array(&alloc_core_bitmap);

	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set(struct job_record *job_ptr)
{
	int rc;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
#if _DEBUG
	info("%s: %pJ", __func__, job_ptr);
#endif
	if (IS_JOB_RUNNING(job_ptr))
		rc = common_add_job_to_res(job_ptr, 0);
	else if (IS_JOB_SUSPENDED(job_ptr)) {
		if (job_ptr->priority == 0)
			rc = common_add_job_to_res(job_ptr, 1);
		else	/* Gang schedule suspend */
			rc = common_add_job_to_res(job_ptr, 0);
	} else
		return SLURM_SUCCESS;
	gres_plugin_job_state_log(job_ptr->gres_list, job_ptr->job_id);

	return rc;
}

extern int select_p_select_nodeinfo_get(select_nodeinfo_t *nodeinfo,
					enum select_nodedata_type dinfo,
					enum node_states state,
					void *data)
{
	int rc = SLURM_SUCCESS;
	uint16_t *uint16 = (uint16_t *) data;
	uint64_t *uint64 = (uint64_t *) data;
	char **tmp_char = (char **) data;
	double *tmp_double = (double *) data;
	select_nodeinfo_t **select_nodeinfo = (select_nodeinfo_t **) data;

	if (nodeinfo == NULL) {
		error("%s: nodeinfo not set", __func__);
		return SLURM_ERROR;
	}

	if (nodeinfo->magic != NODEINFO_MAGIC) {
		error("%s: jobinfo magic bad", __func__);
		return SLURM_ERROR;
	}

	switch (dinfo) {
	case SELECT_NODEDATA_SUBCNT:
		if (state == NODE_STATE_ALLOCATED)
			*uint16 = nodeinfo->alloc_cpus;
		else
			*uint16 = 0;
		break;
	case SELECT_NODEDATA_PTR:
		*select_nodeinfo = nodeinfo;
		break;
	case SELECT_NODEDATA_MEM_ALLOC:
		*uint64 = nodeinfo->alloc_memory;
		break;
	case SELECT_NODEDATA_TRES_ALLOC_FMT_STR:
		*tmp_char = xstrdup(nodeinfo->tres_alloc_fmt_str);
		break;
	case SELECT_NODEDATA_TRES_ALLOC_WEIGHTED:
		*tmp_double = nodeinfo->tres_alloc_weighted;
		break;
	default:
		error("%s: Unsupported option %d", __func__, dinfo);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_alloc(void)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_free(select_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_set(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_get(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	return SLURM_ERROR;
}

/* Unused for this plugin */
extern select_jobinfo_t *select_p_select_jobinfo_copy(select_jobinfo_t *jobinfo)
{
	return NULL;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_pack(select_jobinfo_t *jobinfo, Buf buffer,
					uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_unpack(select_jobinfo_t *jobinfo,
					  Buf buffer,
					  uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern char *select_p_select_jobinfo_sprint(select_jobinfo_t *jobinfo,
					    char *buf, size_t size, int mode)
{
	if (buf && size) {
		buf[0] = '\0';
		return buf;
	}
	return NULL;
}

/* Unused for this plugin */
extern char *select_p_select_jobinfo_xstrdup(select_jobinfo_t *jobinfo,
					     int mode)
{
	return NULL;
}

extern int select_p_get_info_from_plugin(enum select_plugindata_info info,
					 struct job_record *job_ptr,
					 void *data)
{
	int rc = SLURM_SUCCESS;
	uint32_t *tmp_32 = (uint32_t *) data;
	List *tmp_list = (List *) data;

	switch (info) {
	case SELECT_CR_PLUGIN:
		*tmp_32 = SELECT_TYPE_CONS_TRES;
		break;
	case SELECT_CONFIG_INFO:
		*tmp_list = NULL;
		break;
	case SELECT_SINGLE_JOB_TEST:
		*tmp_32 = 1;
		break;
	default:
		error("%s: info type %d invalid", __func__, info);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int select_p_update_node_config(int index)
{
	if (index >= select_node_cnt) {
		error("%s: index too large (%d > %d)", __func__, index,
		      select_node_cnt);
		return SLURM_ERROR;
	}

	/*
	 * Socket and core count can be changed when KNL node reboots in a
	 * different NUMA configuration
	 */
	if ((select_fast_schedule == 1) &&
	    (select_node_record[index].sockets !=
	     select_node_record[index].node_ptr->config_ptr->sockets) &&
	    (select_node_record[index].cores !=
	     select_node_record[index].node_ptr->config_ptr->cores) &&
	    ((select_node_record[index].sockets *
	      select_node_record[index].cores) ==
	     (select_node_record[index].node_ptr->sockets *
	      select_node_record[index].node_ptr->cores))) {
		select_node_record[index].cores =
			select_node_record[index].node_ptr->config_ptr->cores;
		select_node_record[index].sockets =
			select_node_record[index].node_ptr->config_ptr->sockets;
		select_node_record[index].tot_sockets =
			select_node_record[index].boards *
			select_node_record[index].sockets;
	}

	if (select_fast_schedule)
		return SLURM_SUCCESS;

	select_node_record[index].real_memory =
		select_node_record[index].node_ptr->real_memory;
	select_node_record[index].mem_spec_limit =
		select_node_record[index].node_ptr->mem_spec_limit;

	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_update_node_state(struct node_record *node_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_reconfigure(void)
{
	return common_reconfig();
}

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
