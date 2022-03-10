/*****************************************************************************
 *  gang.c - Gang scheduler functions.
 *****************************************************************************
 *  Copyright (C) 2008 Hewlett-Packard Development Company, L.P.
 *  Portions Copyright (C) 2010 SchedMD <https://www.schedmd.com>.
 *  Written by Chris Holmes
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

/*
 * gang scheduler plugin for SLURM
 */

#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include "./gang.h"
#include "slurm/slurm.h"
#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/slurmctld.h"

/* global timeslicer thread variables */
static bool thread_running = false;
static bool thread_shutdown = false;
static pthread_mutex_t term_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  term_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t timeslicer_thread_id = (pthread_t) 0;
static List preempt_job_list = (List) NULL;

/* timeslicer flags and structures */
enum entity_type {
	GS_NODE,
	GS_SOCKET,
	GS_CORE,
	GS_CPU,		/* Without task affinity */
	GS_CPU2		/* With task affinity */
};

enum gs_flags {
	GS_SUSPEND,
	GS_RESUME,
	GS_NO_PART,
	GS_SUCCESS,
	GS_ACTIVE,
	GS_NO_ACTIVE,
	GS_FILLER
};

struct gs_job {
	uint32_t job_id;
	job_record_t *job_ptr;
	uint16_t sig_state;
	uint16_t row_state;
};

struct gs_part {
	char *part_name;
	uint16_t priority;	/* Job priority tier */
	uint32_t num_jobs;
	struct gs_job **job_list;
	uint32_t job_list_size;
	uint32_t num_shadows;
	struct gs_job **shadow;  /* see '"Shadow" Design' below */
	uint32_t shadow_size;
	uint32_t jobs_active;
	bitstr_t *active_resmap;
	uint16_t *active_cpus;
	uint16_t array_size;
	struct gs_part *next;
};

/******************************************
 *
 *       SUMMARY OF DATA MANAGEMENT
 *
 * For GS_CORE:   job_ptr->job_resrcs->{node,core}_bitmap
 * For GS_CPU:    job_ptr->job_resrcs->{node_bitmap, cpus}
 * For GS_CPU2:   job_ptr->job_resrcs->{node,core}_bitmap
 * For GS_SOCKET: job_ptr->job_resrcs->{node,core}_bitmap
 * For GS_NODE:   job_ptr->job_resrcs->node_bitmap only
 *
 *	 EVALUATION ALGORITHM
 *
 * For GS_NODE, GS_SOCKET, GS_CORE, and GS_CPU2 the bits CANNOT conflict
 * For GS_CPU:  if bits conflict, make sure sum of CPUs per
 *              resource don't exceed physical resource count
 *
 *
 * The core_bitmap and cpus array are a collection of allocated values
 * ONLY. For every bit set in node_bitmap, there is a corresponding
 * element in cpus and a set of elements in the core_bitmap.
 *
 ******************************************
 *
 *	"Shadow" Design to support Preemption
 *
 * Jobs in higher priority partitions "cast shadows" on the active
 * rows of lower priority partitions. The effect is that jobs that
 * are "caught" in these shadows are preempted (suspended)
 * indefinitely until the "shadow" disappears. When constructing
 * the active row of a partition, any jobs in the 'shadow' array
 * are applied first.
 *
 ******************************************
 */


/* global variables */
static uint32_t timeslicer_seconds = 0;
static uint16_t gr_type = GS_NODE;
static List gs_part_list = NULL;
static uint32_t default_job_list_size = 64;
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint16_t *gs_bits_per_node = NULL;
static uint32_t num_sorted_part = 0;

/* function declarations */
static void *_timeslicer_thread(void *arg);

static char *_print_flag(int flag)
{
	switch (flag) {
		case GS_SUSPEND:
			return "GS_SUSPEND";
		case GS_RESUME:
			return "GS_RESUME";
		case GS_NO_PART:
			return "GS_NO_PART";
		case GS_SUCCESS:
			return "GS_SUCCESS";
		case GS_ACTIVE:
			return "GS_ACTIVE";
		case GS_NO_ACTIVE:
			return "GS_NO_ACTIVE";
		case GS_FILLER:
			return "GS_FILLER";
		default:
			return "unknown";
	}
}


static void _print_jobs(struct gs_part *p_ptr)
{
	int i;

	if (slurm_conf.debug_flags & DEBUG_FLAG_GANG) {
		info("gang:  part %s has %u jobs, %u shadows:",
		     p_ptr->part_name, p_ptr->num_jobs, p_ptr->num_shadows);
		for (i = 0; i < p_ptr->num_shadows; i++) {
			info("gang:   shadow %pJ row_s %s, sig_s %s",
			     p_ptr->shadow[i]->job_ptr,
			     _print_flag(p_ptr->shadow[i]->row_state),
			     _print_flag(p_ptr->shadow[i]->sig_state));
		}
		for (i = 0; i < p_ptr->num_jobs; i++) {
			info("gang:   %pJ row_s %s, sig_s %s",
			     p_ptr->job_list[i]->job_ptr,
			     _print_flag(p_ptr->job_list[i]->row_state),
			     _print_flag(p_ptr->job_list[i]->sig_state));
		}
		if (p_ptr->active_resmap) {
			int s = bit_size(p_ptr->active_resmap);
			i = bit_set_count(p_ptr->active_resmap);
			info("gang:  active resmap has %d of %d bits set",
		  	     i, s);
		}
	}
}

static uint16_t _get_gr_type(void)
{
	if (slurm_conf.select_type_param & CR_CORE)
		return GS_CORE;
	if (slurm_conf.select_type_param & CR_CPU) {
		if (!xstrcmp(slurm_conf.task_plugin, "task/none"))
			return GS_CPU;
		return GS_CPU2;
	}
	if (slurm_conf.select_type_param & CR_SOCKET)
		return GS_SOCKET;

	/* note that CR_MEMORY is node-level scheduling with
	 * memory management */
	return GS_NODE;
}

static uint16_t _get_part_gr_type(part_record_t *part_ptr)
{
	if (part_ptr) {
		if (part_ptr->cr_type & CR_CORE)
			return GS_CORE;
		if (part_ptr->cr_type & CR_CPU) {
			if (!xstrcmp(slurm_conf.task_plugin, "task/none"))
				return GS_CPU;
			return GS_CPU2;
		}
		if (part_ptr->cr_type & CR_SOCKET)
			return GS_SOCKET;
	}

	/* Use global configuration */
	return gr_type;
}

/* For GS_CPU and GS_CPU2 gs_bits_per_node is the total number of CPUs per node.
 * For GS_CORE and GS_SOCKET gs_bits_per_node is the total number of
 *	cores per per node.
 */
static void _load_phys_res_cnt(void)
{
	uint16_t bit = 0, sock = 0;
	uint32_t i, bit_index = 0;
	node_record_t *node_ptr;

	xfree(gs_bits_per_node);

	if (gr_type == GS_NODE)
		return;

	gs_bits_per_node = xmalloc(node_record_count * sizeof(uint16_t));

	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (gr_type == GS_CPU) {
			bit = node_ptr->config_ptr->cpus;
		} else {
			sock = node_ptr->config_ptr->tot_sockets;
			bit  = node_ptr->config_ptr->cores * sock;
		}

		gs_bits_per_node[bit_index++] = bit;
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_GANG) {
		for (i = 0; i < bit_index; i++) {
			info("gang: _load_phys_res_cnt: bits_per_node[%d]=%u",
			     i, gs_bits_per_node[i]);
		}
	}
}

static uint16_t _get_phys_bit_cnt(int node_index)
{
	node_record_t *node_ptr = node_record_table_ptr + node_index;

	if (gr_type == GS_CPU)
		return node_ptr->config_ptr->cpus;
	return node_ptr->config_ptr->cores *
		node_ptr->config_ptr->tot_sockets;
}

static uint16_t _get_socket_cnt(int node_index)
{
	node_record_t *node_ptr = node_record_table_ptr + node_index;

	return node_ptr->config_ptr->tot_sockets;
}

static void _destroy_parts(void *x)
{
	int i;
	struct gs_part *gs_part_ptr = (struct gs_part *) x;

	xfree(gs_part_ptr->part_name);
	for (i = 0; i < gs_part_ptr->num_jobs; i++)
		xfree(gs_part_ptr->job_list[i]);
	xfree(gs_part_ptr->shadow);
	FREE_NULL_BITMAP(gs_part_ptr->active_resmap);
	xfree(gs_part_ptr->active_cpus);
	xfree(gs_part_ptr->job_list);
	xfree(gs_part_ptr);
}

/* Build the gs_part_list. The job_list will be created later,
 * once a job is added. */
static void _build_parts(void)
{
	ListIterator part_iterator;
	part_record_t *p_ptr;
	struct gs_part *gs_part_ptr;
	int num_parts;

	FREE_NULL_LIST(gs_part_list);

	/* reset the sorted list, since it's currently
	 * pointing to partitions we just destroyed */
	num_sorted_part = 0;

	num_parts = list_count(part_list);
	if (num_parts == 0)
		return;

	gs_part_list = list_create(_destroy_parts);
	part_iterator = list_iterator_create(part_list);
	while ((p_ptr = list_next(part_iterator))) {
		gs_part_ptr = xmalloc(sizeof(struct gs_part));
		gs_part_ptr->part_name = xstrdup(p_ptr->name);
		gs_part_ptr->priority = p_ptr->priority_tier;
		/* everything else is already set to zero/NULL */
		list_append(gs_part_list, gs_part_ptr);
	}
	list_iterator_destroy(part_iterator);
}

/* Find the gs_part entity with the given name */
static int _find_gs_part(void *x, void *key)
{
	struct gs_part *gs_part_ptr = (struct gs_part *) x;
	char *name = (char *) key;
	if (!xstrcmp(name, gs_part_ptr->part_name))
		return 1;
	return 0;
}

/* Find the job_list index of the given job_id in the given partition */
static int _find_job_index(struct gs_part *p_ptr, uint32_t job_id)
{
	int i;
	for (i = 0; i < p_ptr->num_jobs; i++) {
		if (p_ptr->job_list[i]->job_ptr->job_id == job_id)
			return i;
	}
	return -1;
}

/* Return 1 if job "cpu count" fits in this row, else return 0 */
static int _can_cpus_fit(job_record_t *job_ptr, struct gs_part *p_ptr)
{
	int i, j, size;
	uint16_t *p_cpus, *j_cpus;
	job_resources_t *job_res = job_ptr->job_resrcs;

	if (gr_type != GS_CPU)
		return 0;

	size = bit_size(job_res->node_bitmap);
	p_cpus = p_ptr->active_cpus;
	j_cpus = job_res->cpus;

	if (!p_cpus || !j_cpus)
		return 0;

	for (j = 0, i = 0; i < size; i++) {
		if (bit_test(job_res->node_bitmap, i)) {
			if (p_cpus[i]+j_cpus[j] > _get_phys_bit_cnt(i))
				return 0;
			j++;
		}
	}
	return 1;
}


/* Return 1 if job fits in this row, else return 0 */
static int _job_fits_in_active_row(job_record_t *job_ptr,
				   struct gs_part *p_ptr)
{
	job_resources_t *job_res = job_ptr->job_resrcs;
	int count;
	bitstr_t *job_map;
	uint16_t job_gr_type;

	if ((p_ptr->active_resmap == NULL) || (p_ptr->jobs_active == 0))
		return 1;

	job_gr_type = _get_part_gr_type(job_ptr->part_ptr);
	if ((job_gr_type == GS_CPU2) || (job_gr_type == GS_CORE) ||
	    (job_gr_type == GS_SOCKET)) {
		return job_fits_into_cores(job_res, p_ptr->active_resmap,
					   gs_bits_per_node);
	}

	/* job_gr_type == GS_NODE || job_gr_type == GS_CPU */
	job_map = bit_copy(job_res->node_bitmap);
	bit_and(job_map, p_ptr->active_resmap);
	/* any set bits indicate contention for the same resource */
	count = bit_set_count(job_map);
	log_flag(GANG, "gang: %s: %d bits conflict", __func__, count);
	FREE_NULL_BITMAP(job_map);
	if (count == 0)
		return 1;
	if (job_gr_type == GS_CPU) {
		/* For GS_CPU we check the CPU arrays */
		return _can_cpus_fit(job_ptr, p_ptr);
	}

	return 0;
}


/* a helper function for _add_job_to_active when GS_SOCKET
 * a job has just been added to p_ptr->active_resmap, so set all cores of
 * each used socket to avoid activating another job on the same socket */
static void _fill_sockets(bitstr_t *job_nodemap, struct gs_part *p_ptr)
{
	uint32_t c, i;
	int n, first_bit, last_bit;

	if (!job_nodemap || !p_ptr || !p_ptr->active_resmap)
		return;
	first_bit = bit_ffs(job_nodemap);
	last_bit  = bit_fls(job_nodemap);
	if ((first_bit < 0) || (last_bit < 0))
		fatal("gang: _afill_sockets: nodeless job?");

	for (c = 0, n = 0; n < first_bit; n++) {
		c += _get_phys_bit_cnt(n);
	}
	for (n = first_bit; n <= last_bit; n++) {
		uint16_t s, socks, cps, cores_per_node;
		cores_per_node = _get_phys_bit_cnt(n);
		if (bit_test(job_nodemap, n) == 0) {
			c += cores_per_node;
			continue;
		}
		socks = _get_socket_cnt(n);
		cps = cores_per_node / socks;
		for (s = 0; s < socks; s++) {
			for (i = c; i < c+cps; i++) {
				if (bit_test(p_ptr->active_resmap, i))
					break;
			}
			if (i < c+cps) {
				/* set all bits on this used socket */
				bit_nset(p_ptr->active_resmap, c, c+cps-1);
			}
			c += cps;
		}
	}
}


/* Add the given job to the "active" structures of
 * the given partition and increment the run count */
static void _add_job_to_active(job_record_t *job_ptr, struct gs_part *p_ptr)
{
	job_resources_t *job_res = job_ptr->job_resrcs;
	uint16_t job_gr_type;

	/* add job to active_resmap */
	job_gr_type = _get_part_gr_type(job_ptr->part_ptr);
	if ((job_gr_type == GS_CPU2) || (job_gr_type == GS_CORE) ||
	    (job_gr_type == GS_SOCKET)) {
		if (p_ptr->jobs_active == 0 && p_ptr->active_resmap) {
			uint32_t size = bit_size(p_ptr->active_resmap);
			bit_nclear(p_ptr->active_resmap, 0, size-1);
		}
		add_job_to_cores(job_res, &(p_ptr->active_resmap),
				 gs_bits_per_node);
		if (job_gr_type == GS_SOCKET)
			_fill_sockets(job_res->node_bitmap, p_ptr);
	} else { /* GS_NODE or GS_CPU */
		if (!p_ptr->active_resmap) {
			log_flag(GANG, "gang: %s: %pJ first",
				 __func__, job_ptr);
			p_ptr->active_resmap = bit_copy(job_res->node_bitmap);
		} else if (p_ptr->jobs_active == 0) {
			log_flag(GANG, "gang: %s: %pJ copied",
				 __func__, job_ptr);
			bit_copybits(p_ptr->active_resmap,
				     job_res->node_bitmap);
		} else {
			log_flag(GANG, "gang: %s: adding %pJ",
				 __func__, job_ptr);
			bit_or(p_ptr->active_resmap, job_res->node_bitmap);
		}
	}

	/* add job to the active_cpus array */
	if (job_gr_type == GS_CPU) {
		uint32_t i, a, sz = bit_size(p_ptr->active_resmap);
		if (!p_ptr->active_cpus) {
			/* create active_cpus array */
			p_ptr->active_cpus = xmalloc(sz * sizeof(uint16_t));
		}
		if (p_ptr->jobs_active == 0) {
			/* overwrite the existing values in active_cpus */
			for (a = 0, i = 0; i < sz; i++) {
				if (bit_test(job_res->node_bitmap, i)) {
					p_ptr->active_cpus[i] =
						job_res->cpus[a++];
				} else {
					p_ptr->active_cpus[i] = 0;
				}
			}
		} else {
			/* add job to existing jobs in the active cpus */
			for (a = 0, i = 0; i < sz; i++) {
				if (bit_test(job_res->node_bitmap, i)) {
					uint16_t limit = _get_phys_bit_cnt(i);
					p_ptr->active_cpus[i] +=
						job_res->cpus[a++];
					/* when adding shadows, the resources
					 * may get overcommitted */
					if (p_ptr->active_cpus[i] > limit)
						p_ptr->active_cpus[i] = limit;
				}
			}
		}
	}
	p_ptr->jobs_active += 1;
}

static int _suspend_job(job_record_t *job_ptr)
{
	int rc;
	suspend_msg_t msg;

	memset(&msg, 0, sizeof(msg));
	msg.job_id = job_ptr->job_id;
	msg.job_id_str = NULL;
	msg.op = SUSPEND_JOB;
	rc = job_suspend(&msg, 0, -1, false, NO_VAL16);
	/* job_suspend() returns ESLURM_DISABLED if job is already suspended */
	if (rc == SLURM_SUCCESS) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_GANG)
			info("gang: suspending %pJ", job_ptr);
		else
			debug("gang: suspending %pJ", job_ptr);
	} else if (rc != ESLURM_DISABLED) {
		info("gang: suspending %pJ: %s", job_ptr, slurm_strerror(rc));
	}
	return rc;
}

static void _resume_job(job_record_t *job_ptr)
{
	int rc;
	suspend_msg_t msg;

	memset(&msg, 0, sizeof(msg));
	msg.job_id = job_ptr->job_id;
	msg.job_id_str = NULL;
	msg.op = RESUME_JOB;
	rc = job_suspend(&msg, 0, -1, false, NO_VAL16);
	if (rc == SLURM_SUCCESS) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_GANG)
			info("gang: resuming %pJ", job_ptr);
		else
			debug("gang: resuming %pJ", job_ptr);
	} else if (rc != ESLURM_ALREADY_DONE) {
		error("gang: resuming %pJ: %s", job_ptr, slurm_strerror(rc));
	}
}

static void _preempt_job_queue(uint32_t job_id)
{
	uint32_t *tmp_id = xmalloc(sizeof(uint32_t));
	*tmp_id = job_id;
	list_append(preempt_job_list, tmp_id);
}

static void _preempt_job_dequeue(void)
{
	job_record_t *job_ptr;
	uint32_t job_id, *tmp_id;
	uint16_t preempt_mode;

	xassert(preempt_job_list);
	while ((tmp_id = list_pop(preempt_job_list))) {
		int rc = SLURM_ERROR;
		job_id = *tmp_id;
		xfree(tmp_id);

		if ((job_ptr = find_job_record(job_id)) == NULL) {
			error("%s could not find JobId=%u",
			      __func__, job_id);
			continue;
		}
		preempt_mode = slurm_job_preempt_mode(job_ptr);

		if (preempt_mode == PREEMPT_MODE_SUSPEND) {
			if ((rc = _suspend_job(job_ptr)) == ESLURM_DISABLED)
				rc = SLURM_SUCCESS;
		} else if (preempt_mode == PREEMPT_MODE_CANCEL) {
			rc = job_signal(job_ptr, SIGKILL, 0, 0, true);
			if (rc == SLURM_SUCCESS) {
				info("preempted %pJ has been killed", job_ptr);
			}
		} else if ((preempt_mode == PREEMPT_MODE_REQUEUE) &&
			   job_ptr->batch_flag && job_ptr->details &&
			   (job_ptr->details->requeue > 0)) {
			rc = job_requeue(0, job_ptr->job_id, NULL, true, 0);
			if (rc == SLURM_SUCCESS) {
				info("preempted %pJ has been requeued",
				     job_ptr);
			} else
				error("preempted %pJ could not be requeued: %s",
				      job_ptr, slurm_strerror(rc));
		} else if (preempt_mode == PREEMPT_MODE_OFF) {
			error("Invalid preempt_mode %u for %pJ",
			      preempt_mode, job_ptr);
			continue;
		}

		if (rc != SLURM_SUCCESS) {
			rc = job_signal(job_ptr, SIGKILL, 0, 0, true);
			if (rc == SLURM_SUCCESS)
				info("%s: preempted %pJ had to be killed",
				     __func__,job_ptr);
			else {
				info("%s: preempted %pJ kill failure %s",
				     __func__, job_ptr, slurm_strerror(rc));
			}
		}
	}

	return;
}

/* This is the reverse order defined by list.h so to generated a list in
 *	descending order rather than ascending order */
static int _sort_partitions(void *part1, void *part2)
{
	struct gs_part *g1;
	struct gs_part *g2;
	int prio1;
	int prio2;

	g1 = *(struct gs_part **)part1;
	g2 = *(struct gs_part **)part2;

	prio1 = g1->priority;
	prio2 = g2->priority;

	return prio2 - prio1;
}

/* Scan the partition list. Add the given job as a "shadow" to every
 * partition with a lower priority than the given partition */
static void _cast_shadow(struct gs_job *j_ptr, uint16_t priority)
{
	ListIterator part_iterator;
	struct gs_part *p_ptr;
	int i;

	part_iterator = list_iterator_create(gs_part_list);
	while ((p_ptr = list_next(part_iterator))) {
		if (p_ptr->priority >= priority)
			continue;

		/* This partition has a lower priority, so add
		 * the job as a "Shadow" */
		if (!p_ptr->shadow) {
			p_ptr->shadow_size = default_job_list_size;
			p_ptr->shadow = xmalloc(p_ptr->shadow_size *
						sizeof(struct gs_job *));
			/* 'shadow' is initialized to be NULL filled */
		} else {
			/* does this shadow already exist? */
			for (i = 0; i < p_ptr->num_shadows; i++) {
				if (p_ptr->shadow[i] == j_ptr)
					break;
			}
			if (i < p_ptr->num_shadows)
				continue;
		}

		if (p_ptr->num_shadows+1 >= p_ptr->shadow_size) {
			p_ptr->shadow_size *= 2;
			xrealloc(p_ptr->shadow, p_ptr->shadow_size *
						sizeof(struct gs_job *));
		}
		p_ptr->shadow[p_ptr->num_shadows++] = j_ptr;
	}
	list_iterator_destroy(part_iterator);
}


/* Remove the given job as a "shadow" from all partitions */
static void _clear_shadow(struct gs_job *j_ptr)
{
	ListIterator part_iterator;
	struct gs_part *p_ptr;
	int i;

	part_iterator = list_iterator_create(gs_part_list);
	while ((p_ptr = list_next(part_iterator))) {
		if (!p_ptr->shadow)
			continue;

		for (i = 0; i < p_ptr->num_shadows; i++) {
			if (p_ptr->shadow[i] == j_ptr)
				break;
		}
		if (i >= p_ptr->num_shadows)
			/* job not found */
			continue;

		p_ptr->num_shadows--;

		/* shift all other jobs down */
		for (; i < p_ptr->num_shadows; i++)
			p_ptr->shadow[i] = p_ptr->shadow[i+1];
		p_ptr->shadow[p_ptr->num_shadows] = NULL;
	}
	 list_iterator_destroy(part_iterator);
}


/* Rebuild the active row BUT preserve the order of existing jobs.
 * This is called after one or more jobs have been removed from
 * the partition or if a higher priority "shadow" has been added
 * which could preempt running jobs.
 */
static void _update_active_row(struct gs_part *p_ptr, int add_new_jobs)
{
	int i;
	struct gs_job *j_ptr;
	uint16_t preempt_mode;

	log_flag(GANG, "gang: update_active_row: rebuilding part %s...",
		 p_ptr->part_name);
	/* rebuild the active row, starting with any shadows */
	p_ptr->jobs_active = 0;
	for (i = 0; p_ptr->shadow && p_ptr->shadow[i]; i++) {
		_add_job_to_active(p_ptr->shadow[i]->job_ptr, p_ptr);
	}

	/* attempt to add the existing 'active' jobs */
	for (i = 0; i < p_ptr->num_jobs; i++) {
		j_ptr = p_ptr->job_list[i];
		if (j_ptr->row_state != GS_ACTIVE)
			continue;
		if (_job_fits_in_active_row(j_ptr->job_ptr, p_ptr)) {
			_add_job_to_active(j_ptr->job_ptr, p_ptr);
			_cast_shadow(j_ptr, p_ptr->priority);

		} else {
			/* this job has been preempted by a shadow job.
			 * suspend it and preserve it's job_list order */
			if (j_ptr->sig_state != GS_SUSPEND) {
				preempt_mode =
					slurm_job_preempt_mode(j_ptr->job_ptr);
				if (p_ptr->num_shadows &&
				    (preempt_mode != PREEMPT_MODE_OFF) &&
				    (preempt_mode != PREEMPT_MODE_SUSPEND)) {
					_preempt_job_queue(j_ptr->job_id);
				} else
					_suspend_job(j_ptr->job_ptr);
				j_ptr->sig_state = GS_SUSPEND;
				_clear_shadow(j_ptr);
			}
			j_ptr->row_state = GS_NO_ACTIVE;
		}
	}
	/* attempt to add the existing 'filler' jobs */
	for (i = 0; i < p_ptr->num_jobs; i++) {
		j_ptr = p_ptr->job_list[i];
		if (j_ptr->row_state != GS_FILLER)
			continue;
		if (_job_fits_in_active_row(j_ptr->job_ptr, p_ptr)) {
			_add_job_to_active(j_ptr->job_ptr, p_ptr);
			_cast_shadow(j_ptr, p_ptr->priority);
		} else {
			/* this job has been preempted by a shadow job.
			 * suspend it and preserve it's job_list order */
			if (j_ptr->sig_state != GS_SUSPEND) {
				preempt_mode =
					slurm_job_preempt_mode(j_ptr->job_ptr);
				if (p_ptr->num_shadows &&
				    (preempt_mode != PREEMPT_MODE_OFF) &&
				    (preempt_mode != PREEMPT_MODE_SUSPEND)) {
					_preempt_job_queue(j_ptr->job_id);
				} else
					_suspend_job(j_ptr->job_ptr);
				j_ptr->sig_state = GS_SUSPEND;
				_clear_shadow(j_ptr);
			}
			j_ptr->row_state = GS_NO_ACTIVE;
		}
	}

	if (!add_new_jobs)
		return;

	/* attempt to add any new jobs */
	for (i = 0; i < p_ptr->num_jobs; i++) {
		j_ptr = p_ptr->job_list[i];
		if ((j_ptr->row_state != GS_NO_ACTIVE) ||
		    (j_ptr->job_ptr->priority == 0))
			continue;
		if (_job_fits_in_active_row(j_ptr->job_ptr, p_ptr)) {
			_add_job_to_active(j_ptr->job_ptr, p_ptr);
			_cast_shadow(j_ptr, p_ptr->priority);
			/* note that this job is a "filler" for this row,
			 * blocked by a higher priority job */
			j_ptr->row_state = GS_FILLER;
			/* resume the job */
			if (j_ptr->sig_state == GS_SUSPEND) {
				_resume_job(j_ptr->job_ptr);
				j_ptr->sig_state = GS_RESUME;
			}
		}
	}
}

/* rebuild all active rows without reordering jobs:
 * - attempt to preserve running jobs
 * - suspend any jobs that have been "shadowed" (preempted)
 * - resume any "filler" jobs that can be found
 */
static void _update_all_active_rows(void)
{
	ListIterator part_iterator;
 	struct gs_part *p_ptr;

	/* Sort the partitions. This way the shadows of any high-priority
	 * jobs are appropriately adjusted before the lower priority
	 * partitions are updated */
	list_sort(gs_part_list, _sort_partitions);

	part_iterator = list_iterator_create(gs_part_list);
	while ((p_ptr = list_next(part_iterator)))
		_update_active_row(p_ptr, 1);
	list_iterator_destroy(part_iterator);
}

/* remove the given job from the given partition
 * IN job_id - job to remove
 * IN p_ptr  - GS partition structure
 * IN fini   - true is job is in finish state (e.g. not to be resumed)
 */
static void _remove_job_from_part(uint32_t job_id, struct gs_part *p_ptr,
				  bool fini)
{
	int i;
	struct gs_job *j_ptr;

	if (!job_id || !p_ptr)
		return;

	/* find the job in the job_list */
	i = _find_job_index(p_ptr, job_id);
	if (i < 0)
		/* job not found */
		return;

	j_ptr = p_ptr->job_list[i];

	log_flag(GANG, "gang: %s: removing %pJ from %s",
		 __func__, j_ptr->job_ptr, p_ptr->part_name);

	/* remove any shadow first */
	_clear_shadow(j_ptr);

	/* remove the job from the job_list by shifting everyone else down */
	p_ptr->num_jobs--;
	for (; i < p_ptr->num_jobs; i++) {
		p_ptr->job_list[i] = p_ptr->job_list[i+1];
	}
	p_ptr->job_list[i] = NULL;

	/* make sure the job is not suspended by gang, and then delete it */
	if (!fini && (j_ptr->sig_state == GS_SUSPEND) &&
	    j_ptr->job_ptr->priority) {
		log_flag(GANG, "gang: %s: resuming suspended %pJ",
			 __func__, j_ptr->job_ptr);
		_resume_job(j_ptr->job_ptr);
	}
	j_ptr->job_ptr = NULL;
	xfree(j_ptr);

	return;
}

/* Add the given job to the given partition, and if it remains running
 * then "cast it's shadow" over the active row of any partition with a
 * lower priority than the given partition. Return the sig state of the
 * job (GS_SUSPEND or GS_RESUME) */
static uint16_t _add_job_to_part(struct gs_part *p_ptr, job_record_t *job_ptr)
{
	int i;
	struct gs_job *j_ptr;
	uint16_t preempt_mode;

	xassert(p_ptr);
	xassert(job_ptr->job_id > 0);
	xassert(job_ptr->job_resrcs);
	xassert(job_ptr->job_resrcs->node_bitmap);
	xassert(job_ptr->job_resrcs->core_bitmap);

	log_flag(GANG, "gang: %s: adding %pJ to %s",
		 __func__, job_ptr, p_ptr->part_name);

	/* take care of any memory needs */
	if (!p_ptr->job_list) {
		p_ptr->job_list_size = default_job_list_size;
		p_ptr->job_list = xmalloc(p_ptr->job_list_size *
					  sizeof(struct gs_job *));
		/* job_list is initialized to be NULL filled */
	}

	/* protect against duplicates */
	i = _find_job_index(p_ptr, job_ptr->job_id);
	if (i >= 0) {
		/* This job already exists, but the resource allocation
		 * may have changed. In any case, remove the existing
		 * job before adding this new one.
		 */
		log_flag(GANG, "gang: %s: duplicate %pJ detected",
			 __func__, job_ptr);
		_remove_job_from_part(job_ptr->job_id, p_ptr, false);
		_update_active_row(p_ptr, 0);
	}

	/* more memory management */
	if ((p_ptr->num_jobs + 1) == p_ptr->job_list_size) {
		p_ptr->job_list_size *= 2;
		xrealloc(p_ptr->job_list, p_ptr->job_list_size *
			 sizeof(struct gs_job *));
		/* enlarged job_list is initialized to be NULL filled */
	}
	j_ptr = xmalloc(sizeof(struct gs_job));

	/* gather job info */
	j_ptr->job_id    = job_ptr->job_id;
	j_ptr->job_ptr   = job_ptr;
	j_ptr->sig_state = GS_RESUME;  /* all jobs are running initially */
	j_ptr->row_state = GS_NO_ACTIVE; /* job is not in the active row */

	/* append this job to the job_list */
	p_ptr->job_list[p_ptr->num_jobs++] = j_ptr;

	/* determine the immediate fate of this job (run or suspend) */
	if (!IS_JOB_SUSPENDED(job_ptr) &&
	    _job_fits_in_active_row(job_ptr, p_ptr)) {
		log_flag(GANG, "gang: %s: %pJ remains running",
			 __func__, job_ptr);
		_add_job_to_active(job_ptr, p_ptr);
		/* note that this job is a "filler" for this row */
		j_ptr->row_state = GS_FILLER;
		/* all jobs begin in the run state, so
		 * there's no need to signal this job */

		/* since this job is running we need to "cast it's shadow"
		 * over lower priority partitions */
		_cast_shadow(j_ptr, p_ptr->priority);

	} else {
		log_flag(GANG, "gang: %s: suspending %pJ",
			 __func__, job_ptr);
		preempt_mode = slurm_job_preempt_mode(job_ptr);
		if (p_ptr->num_shadows &&
		    (preempt_mode != PREEMPT_MODE_OFF) &&
		    (preempt_mode != PREEMPT_MODE_SUSPEND)) {
			_preempt_job_queue(job_ptr->job_id);
		} else
			_suspend_job(job_ptr);
		j_ptr->sig_state = GS_SUSPEND;
	}

	_print_jobs(p_ptr);

	return j_ptr->sig_state;
}

/* ensure that all jobs running in Slurm are accounted for.
 * this procedure assumes that the gs data has already been
 * locked by the caller!
 */
static void _scan_slurm_job_list(void)
{
	job_record_t *job_ptr;
	struct gs_part *p_ptr;
	int i;
	ListIterator job_iterator;
	char *part_name;

	if (!job_list) {	/* no jobs */
		log_flag(GANG, "gang: %s: job_list NULL", __func__);
		return;
	}
	log_flag(GANG, "gang: %s: job_list exists...", __func__);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		log_flag(GANG, "gang: %s: checking %pJ",
			 __func__, job_ptr);

		/* Exclude HetJobs from gang operation. */
		if (job_ptr->het_job_id)
			continue;

		if (IS_JOB_PENDING(job_ptr))
			continue;
		if (IS_JOB_SUSPENDED(job_ptr) && (job_ptr->priority == 0))
			continue;	/* not suspended by gang */

		if (job_ptr->part_ptr && job_ptr->part_ptr->name)
			part_name = job_ptr->part_ptr->name;
		else
			part_name = job_ptr->partition;

		if (IS_JOB_SUSPENDED(job_ptr) || IS_JOB_RUNNING(job_ptr)) {
			/* are we tracking this job already? */
			p_ptr = list_find_first(gs_part_list, _find_gs_part,
						part_name);
			if (!p_ptr) /* no partition */
				continue;
			i = _find_job_index(p_ptr, job_ptr->job_id);
			if (i >= 0) /* we're tracking it, so continue */
				continue;

			/* We're not tracking this job. Resume it if it's
			 * suspended, and then add it to the job list. */

			_add_job_to_part(p_ptr, job_ptr);
			continue;
		}

		/* if the job is not pending, suspended, or running, then
		 * it's completing or completed. Make sure we've released
		 * this job */
		p_ptr = list_find_first(gs_part_list, _find_gs_part, part_name);
		if (!p_ptr) /* no partition */
			continue;
		_remove_job_from_part(job_ptr->job_id, p_ptr, false);
	}
	list_iterator_destroy(job_iterator);

	/* now that all of the old jobs have been flushed out,
	 * update the active row of all partitions */
	_update_all_active_rows();

	return;
}


/****************************
 * Slurm Timeslicer Hooks
 *
 * Here is a summary of the primary activities that occur
 * within this plugin:
 *
 * gs_init: initialize plugin
 *
 * gs_job_start: a new allocation has been created
 * gs_job_fini: an existing allocation has been cleared
 * gs_reconfig: refresh partition and job data
 * _cycle_job_list: timeslicer thread is rotating jobs
 *
 * gs_fini: terminate plugin
 *
 ***************************/

static void _spawn_timeslicer_thread(void)
{
	slurm_mutex_lock( &thread_flag_mutex );
	if (thread_running) {
		error("timeslicer thread already running, not starting "
		      "another");
		slurm_mutex_unlock(&thread_flag_mutex);
		return;
	}

	slurm_thread_create(&timeslicer_thread_id, _timeslicer_thread, NULL);
	thread_running = true;
	slurm_mutex_unlock(&thread_flag_mutex);
}

/* Initialize data structures and start the gang scheduling thread */
extern void gs_init(void)
{
	if (!(slurm_conf.preempt_mode & PREEMPT_MODE_GANG))
		return;

	if (timeslicer_thread_id)
		return;

	/* initialize global variables */
	log_flag(GANG, "gang: entering gs_init");
	timeslicer_seconds = slurm_conf.sched_time_slice;
	gr_type = _get_gr_type();
	preempt_job_list = list_create(xfree_ptr);

	/* load the physical resource count data */
	_load_phys_res_cnt();

	slurm_mutex_lock(&data_mutex);
	_build_parts();
	/* load any currently running jobs */
	_scan_slurm_job_list();
	slurm_mutex_unlock(&data_mutex);

	/* spawn the timeslicer thread */
	_spawn_timeslicer_thread();
	log_flag(GANG, "gang: leaving gs_init");
}

/* Terminate the gang scheduling thread and free its data structures */
extern void gs_fini(void)
{
	/* terminate the timeslicer thread */
	log_flag(GANG, "gang: entering gs_fini");
	slurm_mutex_lock(&thread_flag_mutex);
	if (thread_running) {
		slurm_mutex_lock(&term_lock);
		thread_shutdown = true;
		slurm_cond_signal(&term_cond);
		slurm_mutex_unlock(&term_lock);
		slurm_mutex_unlock(&thread_flag_mutex);
		usleep(120000);
		if (timeslicer_thread_id)
			error("gang: timeslicer pthread still running");
		else {
			slurm_mutex_lock(&thread_flag_mutex);
			thread_running = false;
			slurm_mutex_unlock(&thread_flag_mutex);
			slurm_mutex_lock(&term_lock);
			thread_shutdown = false;
			slurm_mutex_unlock(&term_lock);
		}
	} else {
		slurm_mutex_unlock(&thread_flag_mutex);
	}

	FREE_NULL_LIST(preempt_job_list);

	slurm_mutex_lock(&data_mutex);
	FREE_NULL_LIST(gs_part_list);
	gs_part_list = NULL;
	xfree(gs_bits_per_node);
	slurm_mutex_unlock(&data_mutex);
	log_flag(GANG, "gang: leaving gs_fini");
}

/* Notify the gang scheduler that a job has been resumed or started.
 * In either case, add the job to gang scheduling. */
extern void gs_job_start(job_record_t *job_ptr)
{
	struct gs_part *p_ptr;
	uint16_t job_sig_state;
	char *part_name;

	if (!(slurm_conf.preempt_mode & PREEMPT_MODE_GANG))
		return;

	/* Exclude HetJobs from gang operation. */
	if (job_ptr->het_job_id)
		return;

	log_flag(GANG, "gang: entering %s for %pJ", __func__, job_ptr);
	/* add job to partition */
	if (job_ptr->part_ptr && job_ptr->part_ptr->name)
		part_name = job_ptr->part_ptr->name;
	else
		part_name = job_ptr->partition;
	slurm_mutex_lock(&data_mutex);
	p_ptr = list_find_first(gs_part_list, _find_gs_part, part_name);
	if (p_ptr) {
		job_sig_state = _add_job_to_part(p_ptr, job_ptr);
		/* if this job is running then check for preemption */
		if (job_sig_state == GS_RESUME)
			_update_all_active_rows();
	}
	slurm_mutex_unlock(&data_mutex);

	if (!p_ptr) {
		/*
		 * No partition was found for this job, so let it run
		 * uninterrupted (what else can we do?)
		 */
		error("gang: could not find partition %s for %pJ",
		      part_name, job_ptr);
	}

	_preempt_job_dequeue();	/* MUST BE OUTSIDE OF data_mutex lock */
	log_flag(GANG, "gang: leaving gs_job_start");
}

/* Gang scheduling has been disabled by change in configuration,
 *	resume any suspended jobs */
extern void gs_wake_jobs(void)
{
	job_record_t *job_ptr;
	ListIterator job_iterator;

	if (!job_list)	/* no jobs */
		return;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		/* Exclude HetJobs from gang operation. */
		if (job_ptr->het_job_id)
			continue;

		if (IS_JOB_SUSPENDED(job_ptr) && (job_ptr->priority != 0)) {
			info("gang waking preempted %pJ", job_ptr);
			_resume_job(job_ptr);
		}
	}
	list_iterator_destroy(job_iterator);
}

/* Notify the gang scheduler that a job has been suspended or completed.
 * In either case, remove the job from gang scheduling. */
extern void gs_job_fini(job_record_t *job_ptr)
{
	struct gs_part *p_ptr;
	char *part_name;

	if (!(slurm_conf.preempt_mode & PREEMPT_MODE_GANG))
		return;

	/* Exclude HetJobs from gang operation. */
	if (job_ptr->het_job_id)
		return;

	log_flag(GANG, "gang: entering %s for %pJ", __func__, job_ptr);
	if (job_ptr->part_ptr && job_ptr->part_ptr->name)
		part_name = job_ptr->part_ptr->name;
	else
		part_name = job_ptr->partition;
	slurm_mutex_lock(&data_mutex);
	p_ptr = list_find_first(gs_part_list, _find_gs_part, part_name);
	if (!p_ptr) {
		slurm_mutex_unlock(&data_mutex);
		log_flag(GANG, "gang: leaving gs_job_fini");
		return;
	}

	/* remove job from the partition */
	_remove_job_from_part(job_ptr->job_id, p_ptr, true);
	/* this job may have preempted other jobs, so
	 * check by updating all active rows */
	_update_all_active_rows();
	slurm_mutex_unlock(&data_mutex);
	log_flag(GANG, "gang: leaving gs_job_fini");
}

/* rebuild data structures from scratch
 *
 * A reconfigure can affect this plugin in these ways:
 * - partitions can be added or removed
 *   - this affects the gs_part_list
 * - nodes can be removed from a partition, or added to a partition
 *   - this affects the size of the active resmap
 *
 * Here's the plan:
 * 1. save a copy of the global structures, and then construct
 *    new ones.
 * 2. load the new partition structures with existing jobs,
 *    confirming the job exists and resizing their resmaps
 *    (if necessary).
 * 3. make sure all partitions are accounted for. If a partition
 *    was removed, make sure any jobs that were in the queue and
 *    that were suspended are resumed. Conversely, if a partition
 *    was added, check for existing jobs that may be contending
 *    for resources that we could begin timeslicing.
 * 4. delete the old global structures and return.
 */
extern void gs_reconfig(void)
{
	int i;
	ListIterator part_iterator;
	struct gs_part *p_ptr, *newp_ptr;
	List old_part_list;
	job_record_t *job_ptr;
	struct gs_job *j_ptr;

	if (!(slurm_conf.preempt_mode & PREEMPT_MODE_GANG))
		return;

	if (!timeslicer_thread_id) {
		/* gs_init() will be called later from read_slurm_conf()
		 * if we are enabling gang scheduling via reconfiguration */
		return;
	}

	log_flag(GANG, "gang: entering gs_reconfig");
	slurm_mutex_lock(&data_mutex);

	old_part_list = gs_part_list;
	gs_part_list = NULL;

	/* reset global data */
	gr_type = _get_gr_type();
	_load_phys_res_cnt();
	_build_parts();

	/* scan the old part list and add existing jobs to the new list */
	part_iterator = list_iterator_create(old_part_list);
	while ((p_ptr = list_next(part_iterator))) {
		newp_ptr = (struct gs_part *) list_find_first(gs_part_list,
							      _find_gs_part,
							      p_ptr->part_name);
		if (!newp_ptr) {
			/* this partition was removed, so resume
			 * any jobs suspended by gang and continue */
			for (i = 0; i < p_ptr->num_jobs; i++) {
				j_ptr = p_ptr->job_list[i];
				if ((j_ptr->sig_state == GS_SUSPEND) &&
				    (j_ptr->job_ptr->priority != 0)) {
					info("resuming job in missing part %s",
					     p_ptr->part_name);
					_resume_job(j_ptr->job_ptr);
					j_ptr->sig_state = GS_RESUME;
				}
			}
			continue;
		}
		if (p_ptr->num_jobs == 0)
			/* no jobs to transfer */
			continue;
		/* we need to transfer the jobs from p_ptr to new_ptr and
		 * adjust their resmaps (if necessary). then we need to create
		 * the active resmap and adjust the state of each job (if
		 * necessary). NOTE: there could be jobs that only overlap
		 * on nodes that are no longer in the partition, but we're
		 * not going to worry about those cases.
		 *
		 * add the jobs from p_ptr into new_ptr in their current order
		 * to preserve the state of timeslicing.
		 */
		for (i = 0; i < p_ptr->num_jobs; i++) {
			job_ptr = find_job_record(p_ptr->job_list[i]->job_id);
			if (job_ptr == NULL) {
				/* job no longer exists in Slurm, so drop it */
				continue;
			}
			if (IS_JOB_SUSPENDED(job_ptr) &&
			    (job_ptr->priority == 0))
				continue;	/* not suspended by gang */
			/* transfer the job as long as it is still active */
			if (IS_JOB_SUSPENDED(job_ptr) ||
			    IS_JOB_RUNNING(job_ptr)) {
				_add_job_to_part(newp_ptr, job_ptr);
			}
		}
	}
	list_iterator_destroy(part_iterator);

	/* confirm all jobs. Scan the master job_list and confirm that we
	 * are tracking all jobs */
	_scan_slurm_job_list();

	FREE_NULL_LIST(old_part_list);
	slurm_mutex_unlock(&data_mutex);

	_preempt_job_dequeue();	/* MUST BE OUTSIDE OF data_mutex lock */
	log_flag(GANG, "gang: leaving gs_reconfig");
}

/************************************
 * Timeslicer Functions
 ***********************************/

/* Build the active row from the job_list.
 * The job_list is assumed to be sorted */
static void _build_active_row(struct gs_part *p_ptr)
{
	int i;
	struct gs_job *j_ptr;

	log_flag(GANG, "gang: entering %s", __func__);
	p_ptr->jobs_active = 0;
	if (p_ptr->num_jobs == 0)
		return;

	/* apply all shadow jobs first */
	for (i = 0; i < p_ptr->num_shadows; i++) {
		_add_job_to_active(p_ptr->shadow[i]->job_ptr, p_ptr);
	}

	/* attempt to add jobs from the job_list in the current order */
	for (i = 0; i < p_ptr->num_jobs; i++) {
		j_ptr = p_ptr->job_list[i];
		if (j_ptr->job_ptr->priority == 0)
			continue;
		if (_job_fits_in_active_row(j_ptr->job_ptr, p_ptr)) {
			_add_job_to_active(j_ptr->job_ptr, p_ptr);
			j_ptr->row_state = GS_ACTIVE;
		}
	}
	log_flag(GANG, "gang: leaving %s", __func__);
}

/* _cycle_job_list
 *
 * This is the heart of the timeslicer. The algorithm works as follows:
 *
 * 1. Each new job is added to the end of the job list, so the earliest job
 *    is at the front of the list.
 * 2. Any "shadow" jobs are first applied to the active_resmap. Then the
 *    active_resmap is filled out by starting with the first job in the list,
 *    and adding to it any job that doesn't conflict with the resources.
 * 3. When the timeslice has passed, all jobs that were added to the active
 *    resmap are moved to the back of the list (preserving their order among
 *    each other).
 * 4. Loop back to step 2, starting with the new "first job in the list".
 */
static void _cycle_job_list(struct gs_part *p_ptr)
{
	int i, j;
	struct gs_job *j_ptr;
	uint16_t preempt_mode;

	log_flag(GANG, "gang: entering %s", __func__);
	/* re-prioritize the job_list and set all row_states to GS_NO_ACTIVE */
	for (i = 0; i < p_ptr->num_jobs; i++) {
		while (p_ptr->job_list[i]->row_state == GS_ACTIVE) {
			/* move this job to the back row and "deactivate" it */
			j_ptr = p_ptr->job_list[i];
			j_ptr->row_state = GS_NO_ACTIVE;
			for (j = i; j+1 < p_ptr->num_jobs; j++) {
				p_ptr->job_list[j] = p_ptr->job_list[j+1];
			}
			p_ptr->job_list[j] = j_ptr;
		}
		if (p_ptr->job_list[i]->row_state == GS_FILLER)
			p_ptr->job_list[i]->row_state = GS_NO_ACTIVE;

	}
	log_flag(GANG, "gang: %s reordered job list:", __func__);
	/* Rebuild the active row. */
	_build_active_row(p_ptr);
	log_flag(GANG, "gang: %s new active job list:", __func__);
	_print_jobs(p_ptr);

	/* Suspend running jobs that are GS_NO_ACTIVE */
	for (i = 0; i < p_ptr->num_jobs; i++) {
		j_ptr = p_ptr->job_list[i];
		if ((j_ptr->row_state == GS_NO_ACTIVE) &&
		    (j_ptr->sig_state == GS_RESUME)) {
			log_flag(GANG, "gang: %s: suspending %pJ",
				 __func__, j_ptr->job_ptr);
			preempt_mode = slurm_job_preempt_mode(j_ptr->job_ptr);
			if (p_ptr->num_shadows &&
			    (preempt_mode != PREEMPT_MODE_OFF) &&
			    (preempt_mode != PREEMPT_MODE_SUSPEND)) {
				_preempt_job_queue(j_ptr->job_id);
			} else
				_suspend_job(j_ptr->job_ptr);
			j_ptr->sig_state = GS_SUSPEND;
			_clear_shadow(j_ptr);
		}
	}

	/* Resume suspended jobs that are GS_ACTIVE */
	for (i = 0; i < p_ptr->num_jobs; i++) {
		j_ptr = p_ptr->job_list[i];
		if ((j_ptr->row_state == GS_ACTIVE) &&
		    (j_ptr->sig_state == GS_SUSPEND) &&
		    (j_ptr->job_ptr->priority != 0)) {	/* Redundant check */
			log_flag(GANG, "gang: %s: resuming %pJ",
				 __func__, j_ptr->job_ptr);
			_resume_job(j_ptr->job_ptr);
			j_ptr->sig_state = GS_RESUME;
			_cast_shadow(j_ptr, p_ptr->priority);
		}
	}
	log_flag(GANG, "gang: leaving %s", __func__);
}

static void _slice_sleep(void)
{
	struct timespec ts = {0, 0};
	struct timeval now;

	gettimeofday(&now, NULL);
	ts.tv_sec = now.tv_sec + timeslicer_seconds;
	ts.tv_nsec = now.tv_usec * 1000;
	slurm_mutex_lock(&term_lock);
	if (!thread_shutdown)
		slurm_cond_timedwait(&term_cond, &term_lock, &ts);
	slurm_mutex_unlock(&term_lock);
}

/* The timeslicer thread */
static void *_timeslicer_thread(void *arg)
{
	/* Write locks on job and read lock on nodes */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK, READ_LOCK };
	ListIterator part_iterator;
	struct gs_part *p_ptr;

	log_flag(GANG, "gang: starting timeslicer loop");
	while (!thread_shutdown) {
		_slice_sleep();
		if (thread_shutdown)
			break;

		lock_slurmctld(job_write_lock);
		slurm_mutex_lock(&data_mutex);
		list_sort(gs_part_list, _sort_partitions);

		/* scan each partition... */
		log_flag(GANG, "gang: %s: scanning partitions", __func__);
		part_iterator = list_iterator_create(gs_part_list);
		while ((p_ptr = list_next(part_iterator))) {
			log_flag(GANG, "gang: %s: part %s: run %u total %u",
				 __func__, p_ptr->part_name,
				 p_ptr->jobs_active, p_ptr->num_jobs);
			if (p_ptr->jobs_active <
			    (p_ptr->num_jobs + p_ptr->num_shadows)) {
				_cycle_job_list(p_ptr);
			}
		}
		list_iterator_destroy(part_iterator);
		slurm_mutex_unlock(&data_mutex);

		/* Preempt jobs that were formerly only suspended */
		_preempt_job_dequeue();	/* MUST BE OUTSIDE data_mutex lock */
		unlock_slurmctld(job_write_lock);
	}

	timeslicer_thread_id = (pthread_t) 0;
	pthread_exit((void *) 0);
	return NULL;
}
