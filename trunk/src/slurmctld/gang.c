/*****************************************************************************
 *  gang.c - Gang scheduler functions.
 *****************************************************************************
 *  Copyright (C) 2008 Hewlett-Packard Development Company, L.P.
 *  Written by Chris Holmes
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

/*
 * gang scheduler plugin for SLURM
 */

#include <pthread.h>
#include <unistd.h>

#include "./gang.h"
#include "slurm/slurm.h"
#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

/* global timeslicer thread variables */
static bool thread_running = false;
static bool thread_shutdown = false;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t timeslicer_thread_id = (pthread_t) 0;
static List preempt_job_list = (List) NULL;

/* timeslicer flags and structures */
enum entity_type {
	GS_NODE,
	GS_SOCKET,
	GS_CORE,
	GS_CPU
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
	struct job_record *job_ptr;
	uint16_t sig_state;
	uint16_t row_state;
};

struct gs_part {
	char *part_name;
	uint16_t priority;
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
 * For GS_NODE:   job_ptr->select_job->node_bitmap only
 * For GS_CPU:    job_ptr->select_job->{node_bitmap, cpus}
 * For GS_SOCKET: job_ptr->select_job->{node,core}_bitmap
 * For GS_CORE:   job_ptr->select_job->{node,core}_bitmap
 *
 *         EVALUATION ALGORITHM
 *
 * For GS_NODE, GS_SOCKET, and GS_CORE, the bits CANNOT conflict
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
 *        "Shadow" Design to support Preemption
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
static uint16_t gs_fast_schedule = 0;
static struct gs_part *gs_part_list = NULL;
static uint32_t default_job_list_size = 64;
static uint32_t gs_resmap_size = 0;
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint16_t *gs_bits_per_node = NULL;
static uint32_t *gs_bit_rep_count = NULL;

static uint16_t *gs_sockets_per_node = NULL;
static uint32_t *gs_socket_rep_count = NULL;

static struct gs_part **gs_part_sorted = NULL;
static uint32_t num_sorted_part = 0;

#define GS_CPU_ARRAY_INCREMENT 8

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
	return "unknown";
}


static void _print_jobs(struct gs_part *p_ptr)
{
	int i;
	debug3("gang:  part %s has %u jobs, %u shadows:",
	       p_ptr->part_name, p_ptr->num_jobs, p_ptr->num_shadows);
	for (i = 0; i < p_ptr->num_shadows; i++) {
		debug3("gang:   shadow job %u row_s %s, sig_s %s",
			p_ptr->shadow[i]->job_ptr->job_id,
			_print_flag(p_ptr->shadow[i]->row_state),
			_print_flag(p_ptr->shadow[i]->sig_state));
	}
	for (i = 0; i < p_ptr->num_jobs; i++) {
		debug3("gang:   job %u row_s %s, sig_s %s",
			p_ptr->job_list[i]->job_ptr->job_id,
			_print_flag(p_ptr->job_list[i]->row_state),
			_print_flag(p_ptr->job_list[i]->sig_state));
	}
	if (p_ptr->active_resmap) {
		int s = bit_size(p_ptr->active_resmap);
		i = bit_set_count(p_ptr->active_resmap);
		debug3("gang:  active resmap has %d of %d bits set", 
		       i, s);
	}
}

static uint16_t _get_gr_type(void)
{
	switch (slurmctld_conf.select_type_param) {
		case CR_CORE:
		case CR_CORE_MEMORY:
			return GS_CORE;
		case CR_CPU:
		case CR_CPU_MEMORY:
			return GS_CPU;
		case CR_SOCKET:
		case CR_SOCKET_MEMORY:
			return GS_SOCKET;
	}
	/* note that CR_MEMORY is node-level scheduling with
	 * memory management */
	return GS_NODE;
}


static void _load_socket_cnt(void)
{
	uint32_t i, index = 0, array_size = GS_CPU_ARRAY_INCREMENT;

	if (gr_type != GS_SOCKET)
		return;

	gs_sockets_per_node = xmalloc(array_size * sizeof(uint16_t));
	gs_socket_rep_count = xmalloc(array_size * sizeof(uint32_t));

	for (i = 0; i < node_record_count; i++) {
		uint16_t sock;
		if (gs_fast_schedule) {
			sock = node_record_table_ptr[i].config_ptr->sockets;
		} else {
			sock = node_record_table_ptr[i].sockets;
		}
		if (gs_sockets_per_node[index] == sock) {
			gs_socket_rep_count[index]++;
			continue;
		}
		if (gs_socket_rep_count[index] > 0) {
			/* advance index and check array_size */
			index++;
			if (index >= array_size) {
				array_size += GS_CPU_ARRAY_INCREMENT;
				xrealloc(gs_sockets_per_node,
				 	array_size * sizeof(uint16_t));
				xrealloc(gs_socket_rep_count,
				 	array_size * sizeof(uint32_t));
			}
		}
		gs_sockets_per_node[index] = sock;
		gs_socket_rep_count[index] = 1;
	}
	index++;
	if (index >= array_size) {
		array_size += GS_CPU_ARRAY_INCREMENT;
		xrealloc(gs_sockets_per_node, array_size * sizeof(uint16_t));
		xrealloc(gs_socket_rep_count, array_size * sizeof(uint32_t));
	}
	/* leave the last entries '0' */

	for (i = 0; i < index; i++) {
		debug3("gang: _load_socket_cnt: grp %d bits %u reps %u",
			i, gs_sockets_per_node[i], gs_socket_rep_count[i]);
	}
}

/* For GS_CPU  the gs_phys_res_cnt is the total number of CPUs per node.
 * For GS_CORE and GS_SOCKET the gs_phys_res_cnt is the total number of
 * cores per per node.
 * This function also sets gs_resmap_size;
 */
static void _load_phys_res_cnt(void)
{
	uint32_t i, index = 0, array_size = GS_CPU_ARRAY_INCREMENT;

	xfree(gs_bits_per_node);
	xfree(gs_bit_rep_count);
	xfree(gs_sockets_per_node);
	xfree(gs_socket_rep_count);

	if ((gr_type != GS_CPU) && (gr_type != GS_CORE) && 
	    (gr_type != GS_SOCKET))
		return;

	gs_bits_per_node = xmalloc(array_size * sizeof(uint16_t));
	gs_bit_rep_count = xmalloc(array_size * sizeof(uint32_t));

	gs_resmap_size = 0;
	for (i = 0; i < node_record_count; i++) {
		uint16_t bit;
		if (gr_type == GS_CPU) {
			if (gs_fast_schedule) {
				bit = node_record_table_ptr[i].config_ptr->
				      cpus;
			} else
				bit = node_record_table_ptr[i].cpus;
		} else {
			if (gs_fast_schedule) {
				bit  = node_record_table_ptr[i].config_ptr->
				       cores;
				bit *= node_record_table_ptr[i].config_ptr->
				       sockets;
			} else {
				bit  = node_record_table_ptr[i].cores;
				bit *= node_record_table_ptr[i].sockets;
			}
		}
		gs_resmap_size += bit;
		if (gs_bits_per_node[index] == bit) {
			gs_bit_rep_count[index]++;
			continue;
		}
		if (gs_bit_rep_count[index] > 0) {
			/* advance index and check array_size */
			index++;
			if (index >= array_size) {
				array_size += GS_CPU_ARRAY_INCREMENT;
				xrealloc(gs_bits_per_node,
				 	array_size * sizeof(uint16_t));
				xrealloc(gs_bit_rep_count,
				 	array_size * sizeof(uint32_t));
			}
		}
		gs_bits_per_node[index] = bit;
		gs_bit_rep_count[index] = 1;
	}
	/* leave the last entries '0' */
	index++;
	if (index >= array_size) {
		array_size += GS_CPU_ARRAY_INCREMENT;
		xrealloc(gs_bits_per_node, array_size * sizeof(uint16_t));
		xrealloc(gs_bit_rep_count, array_size * sizeof(uint32_t));
	}

	for (i = 0; i < index; i++) {
		debug3("gang: _load_phys_res_cnt: grp %d bits %u reps %u",
		       i, gs_bits_per_node[i], gs_bit_rep_count[i]);

	}
	if (gr_type == GS_SOCKET)
		_load_socket_cnt();
}

static uint16_t _get_phys_bit_cnt(int node_index)
{
	int i = 0;
	int pos = gs_bit_rep_count[i++];
	while (node_index >= pos) {
		pos += gs_bit_rep_count[i++];
	}
	return gs_bits_per_node[i-1];
}


static uint16_t _get_socket_cnt(int node_index)
{
	int pos, i = 0;
	if (!gs_socket_rep_count || !gs_sockets_per_node)
		return 0;
	pos = gs_socket_rep_count[i++];
	while (node_index >= pos) {
		pos += gs_socket_rep_count[i++];
	}
	return gs_sockets_per_node[i-1];
}


/* The gs_part_list is a single large array of gs_part entities.
 * To destroy it, step down the array and destroy the pieces of
 * each gs_part entity, and then delete the whole array.
 * To destroy a gs_part entity, you need to delete the name, the
 * list of jobs, the shadow list, and the active_resmap.
 */
static void _destroy_parts(void)
{
	int i;
	struct gs_part *tmp, *ptr = gs_part_list;

	while (ptr) {
		tmp = ptr;
		ptr = ptr->next;

		xfree(tmp->part_name);
		for (i = 0; i < tmp->num_jobs; i++) {
			xfree(tmp->job_list[i]);
		}
		xfree(tmp->shadow);
		if (tmp->active_resmap)
			bit_free(tmp->active_resmap);
		xfree(tmp->active_cpus);
		xfree(tmp->job_list);
	}
	xfree(gs_part_list);
}

/* Build the gs_part_list. The job_list will be created later,
 * once a job is added. */
static void _build_parts(void)
{
	ListIterator part_iterator;
	struct part_record *p_ptr;
	int i, num_parts;

	if (gs_part_list)
		_destroy_parts();

	/* reset the sorted list, since it's currently
	 * pointing to partitions we just destroyed */
	num_sorted_part = 0;

	num_parts = list_count(part_list);
	if (!num_parts)
		return;

	part_iterator = list_iterator_create(part_list);
	if (part_iterator == NULL)
		fatal ("memory allocation failure");

	gs_part_list = xmalloc(num_parts * sizeof(struct gs_part));
	i = 0;
	while ((p_ptr = (struct part_record *) list_next(part_iterator))) {
		gs_part_list[i].part_name = xstrdup(p_ptr->name);
		gs_part_list[i].priority = p_ptr->priority;
		/* everything else is already set to zero/NULL */
		gs_part_list[i].next = &(gs_part_list[i+1]);
		i++;
	}
	gs_part_list[--i].next = NULL;
	list_iterator_destroy(part_iterator);
}

/* Find the gs_part entity with the given name */
static struct gs_part *_find_gs_part(char *name)
{
	struct gs_part *p_ptr = gs_part_list;
	for (; p_ptr; p_ptr = p_ptr->next) {
		if (strcmp(name, p_ptr->part_name) == 0)
			return p_ptr;
	}
	return NULL;
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
static int _can_cpus_fit(struct job_record *job_ptr, struct gs_part *p_ptr)
{
	int i, j, size;
	uint16_t *p_cpus, *j_cpus;
	select_job_res_t *job_res = job_ptr->select_job;

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
static int _job_fits_in_active_row(struct job_record *job_ptr, 
				   struct gs_part *p_ptr)
{
	select_job_res_t *job_res = job_ptr->select_job;
	int count;
	bitstr_t *job_map;

	if ((p_ptr->active_resmap == NULL) || (p_ptr->jobs_active == 0))
		return 1;

	if ((gr_type == GS_CORE) || (gr_type == GS_SOCKET)) {
		return can_select_job_cores_fit(job_res, p_ptr->active_resmap,
						gs_bits_per_node,
						gs_bit_rep_count);
	}
	
	/* gr_type == GS_NODE || gr_type == GS_CPU */
	job_map = bit_copy(job_res->node_bitmap);
	if (!job_map)
		fatal("gang: memory allocation error");
	bit_and(job_map, p_ptr->active_resmap);
	/* any set bits indicate contention for the same resource */
	count = bit_set_count(job_map);
	debug3("gang: _job_fits_in_active_row: %d bits conflict", count);
	bit_free(job_map);
	if (count == 0)
		return 1;
	if (gr_type == GS_CPU)
		/* For GS_CPU we check the CPU arrays */
		return _can_cpus_fit(job_ptr, p_ptr);

	return 0;
}


/* a helper function for _add_job_to_active when GS_SOCKET
 * a job has just been added to p_ptr->active_resmap, so set all cores of
 * each used socket to avoid activating another job on the same socket */
static void _fill_sockets(bitstr_t *job_nodemap, struct gs_part *p_ptr)
{
	uint32_t c, i, size;
	int n, first_bit, last_bit;

	if (!job_nodemap || !p_ptr || !p_ptr->active_resmap)
		return;
	size      = bit_size(job_nodemap);
	first_bit = bit_ffs(job_nodemap);
	last_bit  = bit_fls(job_nodemap);
	if (first_bit < 0 || last_bit < 0)
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
static void _add_job_to_active(struct job_record *job_ptr, 
			       struct gs_part *p_ptr)
{
	select_job_res_t *job_res = job_ptr->select_job;

	/* add job to active_resmap */
	if (gr_type == GS_CORE || gr_type == GS_SOCKET) {
		if (p_ptr->jobs_active == 0 && p_ptr->active_resmap) {
			uint32_t size = bit_size(p_ptr->active_resmap);
			bit_nclear(p_ptr->active_resmap, 0, size-1);
		}
		add_select_job_to_row(job_res, &(p_ptr->active_resmap),
				      gs_bits_per_node, gs_bit_rep_count);
		if (gr_type == GS_SOCKET)
			_fill_sockets(job_res->node_bitmap, p_ptr);
	} else {
		/* GS_NODE or GS_CPU */
		if (!p_ptr->active_resmap) {
			debug3("gang: _add_job_to_active: job %u first",
				job_ptr->job_id);
			p_ptr->active_resmap = bit_copy(job_res->node_bitmap);
		} else if (p_ptr->jobs_active == 0) {
			debug3("gang: _add_job_to_active: job %u copied",
				job_ptr->job_id);
			bit_copybits(p_ptr->active_resmap,
				     job_res->node_bitmap);
		} else {
			debug3("gang: _add_job_to_active: adding job %u",
				job_ptr->job_id);
			bit_or(p_ptr->active_resmap, job_res->node_bitmap);
		}
	}
	
	/* add job to the active_cpus array */
	if (gr_type == GS_CPU) {
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

static int _suspend_job(uint32_t job_id)
{
	int rc;
	suspend_msg_t msg;

	msg.job_id = job_id;
	debug3("gang: suspending %u", job_id);
	msg.op = SUSPEND_JOB;
	rc = job_suspend(&msg, 0, -1, false);
	/* job_suspend() returns ESLURM_DISABLED if job is already suspended */
	if ((rc != SLURM_SUCCESS) && (rc != ESLURM_DISABLED)) {
		info("gang: suspending job %u: %s", 
		     job_id, slurm_strerror(rc));
	}
	return rc;
}

static void _resume_job(uint32_t job_id)
{
	int rc;
	suspend_msg_t msg;

	msg.job_id = job_id;
	debug3("gang: resuming %u", job_id);
	msg.op = RESUME_JOB;
	rc = job_suspend(&msg, 0, -1, false);
	if ((rc != SLURM_SUCCESS) && (rc != ESLURM_ALREADY_DONE)) {
		error("gang: resuming job %u: %s", 
		      job_id, slurm_strerror(rc));
	}
}

static int _cancel_job(uint32_t job_id)
{
	int rc;

	rc = job_signal(job_id, SIGKILL, 0, 0);
	if (rc == SLURM_SUCCESS)
		info("gang: preempted job %u has been killed", job_id);

	return rc;
}
static int _checkpoint_job(uint32_t job_id)
{
	int rc;
	checkpoint_msg_t ckpt_msg;

	/* NOTE: job_checkpoint(VACATE) eventually calls gs_job_fini(),
	 * so we can't process this request in real-time */
	memset(&ckpt_msg, 0, sizeof(checkpoint_msg_t));
	ckpt_msg.op        = CHECK_VACATE;
	rc = job_checkpoint(&ckpt_msg, 0, -1);
	if (rc == SLURM_SUCCESS) {
		info("gang: preempted job %u has been checkpointed",
		     job_id);
	}

	return rc;
}

static int _requeue_job(uint32_t job_id)
{
	int rc;

	/* NOTE: job_requeue eventually calls gs_job_fini(),
	 * so we can't process this request in real-time */
	rc = job_requeue(0, job_id, -1);
	if (rc == SLURM_SUCCESS) {
		info("gang: preempted job %u has been requeued",
		     job_id);
	}

	return rc;
}

void _preempt_job_list_del(void *x)
{
	xfree(x);
}

static void _preempt_job_queue(uint32_t job_id)
{
	uint32_t *tmp_id = xmalloc(sizeof(uint32_t));
	*tmp_id = job_id;
	list_append(preempt_job_list, tmp_id);
}

static void _preempt_job_dequeue(void)
{
	int rc = 0;
	uint32_t job_id, *tmp_id;
	uint16_t preempt_mode = slurm_get_preempt_mode();

	preempt_mode &= (~PREEMPT_MODE_GANG);
	while ((tmp_id = list_pop(preempt_job_list))) {
		job_id = *tmp_id;
		xfree(tmp_id);

		if (preempt_mode == PREEMPT_MODE_SUSPEND)
			(void) _suspend_job(job_id);
		else if (preempt_mode == PREEMPT_MODE_REQUEUE)
			rc = _requeue_job(job_id);
		else if (preempt_mode == PREEMPT_MODE_CANCEL)
			rc = _cancel_job(job_id);
		else if (preempt_mode == PREEMPT_MODE_CHECKPOINT)
			rc = _checkpoint_job(job_id);
		else
			fatal("Invalid preempt_mode: %u", preempt_mode);

		if (rc != SLURM_SUCCESS) {
			rc = job_signal(job_id, SIGKILL, 0, 0);
			if (rc == SLURM_SUCCESS)
				info("gang: preempted job %u had to be killed",
				     job_id);
			else {
				info("gang: preempted job %u kill failure %s", 
				     job_id, slurm_strerror(rc));
			}
		}
	}
	return;
}

/* construct gs_part_sorted as a sorted list of the current partitions */
static void _sort_partitions(void)
{
	struct gs_part *p_ptr;
	int i, j, size = 0;

	/* sort all partitions by priority */
	for (p_ptr = gs_part_list; p_ptr; p_ptr = p_ptr->next, size++);

	/* sorted array is new, or number of partitions has changed */
	if (size != num_sorted_part) {
		xfree(gs_part_sorted);
		gs_part_sorted = xmalloc(size * sizeof(struct gs_part *));
		num_sorted_part = size;
		/* load the array */
		i = 0;
		for (p_ptr = gs_part_list; p_ptr; p_ptr = p_ptr->next)
			gs_part_sorted[i++] = p_ptr;
	}

	if (size <= 1) {
		gs_part_sorted[0] = gs_part_list;
		return;
	}

	/* sort array (new array or priorities may have changed) */
	for (j = 0; j < size; j++) {
		for (i = j+1; i < size; i++) {
			if (gs_part_sorted[i]->priority >
				gs_part_sorted[j]->priority) {
				struct gs_part *tmp_ptr;
				tmp_ptr = gs_part_sorted[j];
				gs_part_sorted[j] = gs_part_sorted[i];
				gs_part_sorted[i] = tmp_ptr;
			}
		}
	}
}


/* Scan the partition list. Add the given job as a "shadow" to every
 * partition with a lower priority than the given partition */
static void _cast_shadow(struct gs_job *j_ptr, uint16_t priority)
{
	struct gs_part *p_ptr;
	int i;
	
	for (p_ptr = gs_part_list; p_ptr; p_ptr = p_ptr->next) {
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
}


/* Remove the given job as a "shadow" from all partitions */
static void _clear_shadow(struct gs_job *j_ptr)
{
	struct gs_part *p_ptr;
	int i;
	
	for (p_ptr = gs_part_list; p_ptr; p_ptr = p_ptr->next) {

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

	debug3("gang: update_active_row: rebuilding part %s...",
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
				if (p_ptr->num_shadows)
					_preempt_job_queue(j_ptr->job_id);
				else
					_suspend_job(j_ptr->job_id);
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
				if (p_ptr->num_shadows)
					_preempt_job_queue(j_ptr->job_id);
				else
					_suspend_job(j_ptr->job_id);
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
		if (j_ptr->row_state != GS_NO_ACTIVE)
			continue;
		if (_job_fits_in_active_row(j_ptr->job_ptr, p_ptr)) {
			_add_job_to_active(j_ptr->job_ptr, p_ptr);
			_cast_shadow(j_ptr, p_ptr->priority);
			/* note that this job is a "filler" for this row,
			 * blocked by a higher priority job */
			j_ptr->row_state = GS_FILLER;
			/* resume the job */
			if (j_ptr->sig_state == GS_SUSPEND) {
				_resume_job(j_ptr->job_id);
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
	int i;
	
	/* Sort the partitions. This way the shadows of any high-priority
	 * jobs are appropriately adjusted before the lower priority
	 * partitions are updated */
	_sort_partitions();
	
	for (i = 0; i < num_sorted_part; i++) {
		_update_active_row(gs_part_sorted[i], 1);
	}
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

	debug3("gang: _remove_job_from_part: removing job %u from %s",
		job_id, p_ptr->part_name);
	j_ptr = p_ptr->job_list[i];
	
	/* remove any shadow first */
	_clear_shadow(j_ptr);
	
	/* remove the job from the job_list by shifting everyone else down */
	p_ptr->num_jobs--;
	for (; i < p_ptr->num_jobs; i++) {
		p_ptr->job_list[i] = p_ptr->job_list[i+1];
	}
	p_ptr->job_list[i] = NULL;
	
	/* make sure the job is not suspended, and then delete it */
	if (!fini && (j_ptr->sig_state == GS_SUSPEND)) {
		debug3("gang: _remove_job_from_part: resuming suspended "
		       "job %u", j_ptr->job_id);
		_resume_job(j_ptr->job_id);
	}
	j_ptr->job_ptr = NULL;
	xfree(j_ptr);
	
	return;
}

/* Add the given job to the given partition, and if it remains running
 * then "cast it's shadow" over the active row of any partition with a
 * lower priority than the given partition. Return the sig state of the
 * job (GS_SUSPEND or GS_RESUME) */
static uint16_t _add_job_to_part(struct gs_part *p_ptr, 
				 struct job_record *job_ptr)
{
	int i;
	struct gs_job *j_ptr;

	xassert(p_ptr);
	xassert(job_ptr->job_id > 0);
	xassert(job_ptr->select_job);
	xassert(job_ptr->select_job->node_bitmap);
	xassert(job_ptr->select_job->core_bitmap);

	debug3("gang: _add_job_to_part: adding job %u to %s",
		job_ptr->job_id, p_ptr->part_name);
	
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
		debug3("gang: _add_job_to_part: duplicate job %u detected",
		       job_ptr->job_id);
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
	if (_job_fits_in_active_row(job_ptr, p_ptr)) {
		debug3("gang: _add_job_to_part: job %u remains running", 
			job_ptr->job_id);
		_add_job_to_active(job_ptr, p_ptr);
		/* note that this job is a "filler" for this row */
		j_ptr->row_state = GS_FILLER;
		/* all jobs begin in the run state, so
		 * there's no need to signal this job */

		/* since this job is running we need to "cast it's shadow"
		 * over lower priority partitions */
		_cast_shadow(j_ptr, p_ptr->priority);

	} else {
		debug3("gang: _add_job_to_part: suspending job %u",
			job_ptr->job_id);
		if (p_ptr->num_shadows)
			_preempt_job_queue(job_ptr->job_id);
		else
			_suspend_job(job_ptr->job_id);
		j_ptr->sig_state = GS_SUSPEND;
	}
	
	_print_jobs(p_ptr);
	
	return j_ptr->sig_state;
}

/* ensure that all jobs running in SLURM are accounted for.
 * this procedure assumes that the gs data has already been
 * locked by the caller! 
 */
static void _scan_slurm_job_list(void)
{
	struct job_record *job_ptr;
	struct gs_part *p_ptr;
	int i;
	ListIterator job_iterator;

	if (!job_list) {	/* no jobs */
		return;
	}
	debug3("gang: _scan_slurm_job_list: job_list exists...");
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		debug3("gang: _scan_slurm_job_list: checking job %u",
			job_ptr->job_id);		
		if (IS_JOB_PENDING(job_ptr))
			continue;
		if (IS_JOB_SUSPENDED(job_ptr) && (job_ptr->priority == 0))
			continue;	/* not suspended by us */

		if (IS_JOB_SUSPENDED(job_ptr) || IS_JOB_RUNNING(job_ptr)) {
			/* are we tracking this job already? */
			p_ptr = _find_gs_part(job_ptr->partition);
			if (!p_ptr) /* no partition */
				continue;
			i = _find_job_index(p_ptr, job_ptr->job_id);
			if (i >= 0)
				/* we're tracking it, so continue */
				continue;
			
			/* We're not tracking this job. Resume it if it's
			 * suspended, and then add it to the job list. */
			
			if (IS_JOB_SUSPENDED(job_ptr)) {
				/* The likely scenario here is that the 
				 * failed over, and this is a job that gang 
				 * had previously suspended. It's not possible 
				 * to determine the previous order of jobs 
				 * without preserving gang state, which is not 
				 * worth the extra infrastructure. Just resume 
				 * the job and then add it to the job list.
				 */
				_resume_job(job_ptr->job_id);
			}

			_add_job_to_part(p_ptr, job_ptr);
			continue;
		}
		
		/* if the job is not pending, suspended, or running, then
		 * it's completing or completed. Make sure we've released
		 * this job */		
		p_ptr = _find_gs_part(job_ptr->partition);
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
 * SLURM Timeslicer Hooks
 *
 * Here is a summary of the primary activities that occur
 * within this plugin:
 *
 * gs_init: initialize plugin
 *
 * gs_job_start: a new allocation has been created
 * gs_job_scan: synchronize with master job list
 * gs_job_fini: an existing allocation has been cleared
 * gs_reconfig: refresh partition and job data
 * _cycle_job_list: timeslicer thread is rotating jobs
 *
 * gs_fini: terminate plugin
 *
 ***************************/

static void _spawn_timeslicer_thread(void)
{
	pthread_attr_t thread_attr_msg;

	pthread_mutex_lock( &thread_flag_mutex );
	if (thread_running) {
		error("timeslicer thread already running, not starting "
		      "another");
		pthread_mutex_unlock(&thread_flag_mutex);
		return;
	}

	slurm_attr_init(&thread_attr_msg);
	if (pthread_create(&timeslicer_thread_id, &thread_attr_msg, 
			_timeslicer_thread, NULL))
		fatal("pthread_create %m");

	slurm_attr_destroy(&thread_attr_msg);
	thread_running = true;
	pthread_mutex_unlock(&thread_flag_mutex);
}

/* Initialize data structures and start the gang scheduling thread */
extern int gs_init(void)
{
	if (timeslicer_thread_id)
		return SLURM_SUCCESS;

	/* initialize global variables */
	debug3("gang: entering gs_init");
	timeslicer_seconds = slurmctld_conf.sched_time_slice;
	gs_fast_schedule = slurm_get_fast_schedule();
	gr_type = _get_gr_type();
	preempt_job_list = list_create(_preempt_job_list_del);

	/* load the physical resource count data */
	_load_phys_res_cnt();

	pthread_mutex_lock(&data_mutex);
	_build_parts();
	/* load any currently running jobs */
	_scan_slurm_job_list();
	pthread_mutex_unlock(&data_mutex);

	/* spawn the timeslicer thread */
	_spawn_timeslicer_thread();
	debug3("gang: leaving gs_init");
	return SLURM_SUCCESS;
}

/* Terminate the gang scheduling thread and free its data structures */
extern int gs_fini(void)
{
	/* terminate the timeslicer thread */
	debug3("gang: entering gs_fini");
	pthread_mutex_lock(&thread_flag_mutex);
	if (thread_running) {
		int i;
		thread_shutdown = true;
		for (i=0; i<4; i++) {
			if (pthread_cancel(timeslicer_thread_id)) {
				timeslicer_thread_id = 0;
				break;
			}
			usleep(1000);
		}
		if (timeslicer_thread_id)
			error("gang: Cound not kill timeslicer pthread");
	}
	pthread_mutex_unlock(&thread_flag_mutex);

	list_destroy(preempt_job_list);

	pthread_mutex_lock(&data_mutex);
	_destroy_parts();
	xfree(gs_part_sorted);
	gs_part_sorted = NULL;
	xfree(gs_bits_per_node);
	xfree(gs_bit_rep_count);
	xfree(gs_sockets_per_node);
	xfree(gs_socket_rep_count);
	pthread_mutex_unlock(&data_mutex);
	debug3("gang: leaving gs_fini");

	return SLURM_SUCCESS;
}

/* Notify the gang scheduler that a job has been started */
extern int gs_job_start(struct job_record *job_ptr)
{
	struct gs_part *p_ptr;
	uint16_t job_state;

	debug3("gang: entering gs_job_start for job %u", job_ptr->job_id);
	/* add job to partition */
	pthread_mutex_lock(&data_mutex);
	p_ptr = _find_gs_part(job_ptr->partition);
	if (p_ptr) {
		job_state = _add_job_to_part(p_ptr, job_ptr);
		/* if this job is running then check for preemption */
		if (job_state == GS_RESUME)
			_update_all_active_rows();
	}
	pthread_mutex_unlock(&data_mutex);

	if (!p_ptr) {
		/* No partition was found for this job, so let it run
		 * uninterupted (what else can we do?)
		 */
		error("gang: could not find partition %s for job %u",
		      job_ptr->partition, job_ptr->job_id);
	}

	_preempt_job_dequeue();	/* MUST BE OUTSIDE OF data_mutex lock */
	debug3("gang: leaving gs_job_start");

	return SLURM_SUCCESS;
}

/* Scan the master SLURM job list for any new jobs to add, or for any old jobs 
 *	to remove */
extern int gs_job_scan(void)
{
	debug3("gang: entering gs_job_scan");
	pthread_mutex_lock(&data_mutex);
	_scan_slurm_job_list();
	pthread_mutex_unlock(&data_mutex);

	_preempt_job_dequeue();	/* MUST BE OUTSIDE OF data_mutex lock */
	debug3("gang: leaving gs_job_scan");

	return SLURM_SUCCESS;
}

/* Gang scheduling has been disabled by change in configuration, 
 *	resume any suspended jobs */
extern void gs_wake_jobs(void)
{
	struct job_record *job_ptr;
	ListIterator job_iterator;

	if (!job_list)	/* no jobs */
		return;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (IS_JOB_SUSPENDED(job_ptr) && (job_ptr->priority != 0)) {
			info("gang waking preempted job %u", job_ptr->job_id); 
			_resume_job(job_ptr->job_id);
		}
	}
	list_iterator_destroy(job_iterator);
}

/* Notify the gang scheduler that a job has completed */
extern int gs_job_fini(struct job_record *job_ptr)
{
	struct gs_part *p_ptr;
	
	debug3("gang: entering gs_job_fini for job %u", job_ptr->job_id);
	pthread_mutex_lock(&data_mutex);
	p_ptr = _find_gs_part(job_ptr->partition);
	if (!p_ptr) {
		pthread_mutex_unlock(&data_mutex);
		debug3("gang: leaving gs_job_fini");
		return SLURM_SUCCESS;
	}

	/* remove job from the partition */
	_remove_job_from_part(job_ptr->job_id, p_ptr, true);
	/* this job may have preempted other jobs, so
	 * check by updating all active rows */
	_update_all_active_rows();
	pthread_mutex_unlock(&data_mutex);
	debug3("gang: leaving gs_job_fini");
	
	return SLURM_SUCCESS;
}

/* rebuild data structures from scratch
 *
 * A reconfigure can affect this plugin in these ways:
 * - partitions can be added or removed
 *   - this affects the gs_part_list
 * - nodes can be removed from a partition, or added to a partition
 *   - this affects the size of the active resmap
 *
 * If nodes have been added or removed, then the node_record_count
 * will be different from gs_resmap_size. In this case, we need
 * to resize the existing resmaps to prevent errors when comparing
 * them.
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
extern int gs_reconfig(void)
{
	int i;
	struct gs_part *p_ptr, *old_part_list, *newp_ptr;
	struct job_record *job_ptr;

	debug3("gang: entering gs_reconfig");
	pthread_mutex_lock(&data_mutex);

	old_part_list = gs_part_list;
	gs_part_list = NULL;

	/* reset global data */
	gs_fast_schedule = slurm_get_fast_schedule();
	gr_type = _get_gr_type();
	_load_phys_res_cnt();
	_build_parts();
	
	/* scan the old part list and add existing jobs to the new list */
	for (p_ptr = old_part_list; p_ptr; p_ptr = p_ptr->next) {
		newp_ptr = _find_gs_part(p_ptr->part_name);
		if (!newp_ptr) {
			/* this partition was removed, so resume
			 * any suspended jobs and continue */
			for (i = 0; i < p_ptr->num_jobs; i++) {
				if (p_ptr->job_list[i]->sig_state == 
				    GS_SUSPEND) {
					info("resuming job in missing part %s",
					     p_ptr->part_name);
					_resume_job(p_ptr->job_list[i]->
						   job_id);
					p_ptr->job_list[i]->sig_state = 
						GS_RESUME;
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
				/* job no longer exists in SLURM, so drop it */
				continue;
			}
			/* resume any job that is suspended by us */
			if (IS_JOB_SUSPENDED(job_ptr) && 
			    (job_ptr->priority != 0)) {
				debug3("resuming job %u apparently suspended "
				       " by gang", job_ptr->job_id);
				_resume_job(job_ptr->job_id);
			}

			/* transfer the job as long as it is still active */
			if (IS_JOB_SUSPENDED(job_ptr) ||
			    IS_JOB_RUNNING(job_ptr)) {
				_add_job_to_part(newp_ptr, job_ptr);
			}
		}
	}

	/* confirm all jobs. Scan the master job_list and confirm that we
	 * are tracking all jobs */
	_scan_slurm_job_list();

	/* Finally, destroy the old data */
	p_ptr = gs_part_list;
	gs_part_list = old_part_list;
	_destroy_parts();
	gs_part_list = p_ptr;

	pthread_mutex_unlock(&data_mutex);

	_preempt_job_dequeue();	/* MUST BE OUTSIDE OF data_mutex lock */
	debug3("gang: leaving gs_reconfig");

	return SLURM_SUCCESS;
}

/************************************
 * Timeslicer Functions
 ***********************************/

/* Build the active row from the job_list.
 * The job_list is assumed to be sorted */
static void _build_active_row(struct gs_part *p_ptr)
{
	int i;
	
	debug3("gang: entering _build_active_row");
	p_ptr->jobs_active = 0;
	if (p_ptr->num_jobs == 0)
		return;
	
	/* apply all shadow jobs first */
	for (i = 0; i < p_ptr->num_shadows; i++) {
		_add_job_to_active(p_ptr->shadow[i]->job_ptr, p_ptr);
	}
	
	/* attempt to add jobs from the job_list in the current order */
	for (i = 0; i < p_ptr->num_jobs; i++) {
		if (_job_fits_in_active_row(p_ptr->job_list[i]->job_ptr, 
					    p_ptr)) {
			_add_job_to_active(p_ptr->job_list[i]->job_ptr, p_ptr);
			p_ptr->job_list[i]->row_state = GS_ACTIVE;
		}
	}
	debug3("gang: leaving _build_active_row");
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
	
	debug3("gang: entering _cycle_job_list");
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
	debug3("gang: _cycle_job_list reordered job list:");
	/* Rebuild the active row. */
	_build_active_row(p_ptr);
	debug3("gang: _cycle_job_list new active job list:");
	_print_jobs(p_ptr);

	/* Suspend running jobs that are GS_NO_ACTIVE */
	for (i = 0; i < p_ptr->num_jobs; i++) {
		j_ptr = p_ptr->job_list[i];
		if ((p_ptr->shadow_size)		||
		    ((j_ptr->row_state == GS_NO_ACTIVE) &&
		     (j_ptr->sig_state == GS_RESUME))) {
		    	debug3("gang: _cycle_job_list: suspending job %u", 
			       j_ptr->job_id);
			if (p_ptr->num_shadows)
				_preempt_job_queue(j_ptr->job_id);
			else
				_suspend_job(j_ptr->job_id);
			j_ptr->sig_state = GS_SUSPEND;
			_clear_shadow(j_ptr);
		}
	}
	
	/* Resume suspended jobs that are GS_ACTIVE */
	for (i = 0; i < p_ptr->num_jobs; i++) {
		j_ptr = p_ptr->job_list[i];
		if (j_ptr->row_state == GS_ACTIVE &&
		    j_ptr->sig_state == GS_SUSPEND) {
		    	debug3("gang: _cycle_job_list: resuming job %u",
				j_ptr->job_id);
			_resume_job(j_ptr->job_id);
			j_ptr->sig_state = GS_RESUME;
			_cast_shadow(j_ptr, p_ptr->priority);
		}
	}
	debug3("gang: leaving _cycle_job_list");
}

/* The timeslicer thread */
static void *_timeslicer_thread(void *arg)
{
	struct gs_part *p_ptr;
	int i;
	
	debug3("gang: starting timeslicer loop");
	while (!thread_shutdown) {
		pthread_mutex_lock(&data_mutex);

		_sort_partitions();
		
		/* scan each partition... */
		debug3("gang: _timeslicer_thread: scanning partitions");
		for (i = 0; i < num_sorted_part; i++) {
			p_ptr = gs_part_sorted[i];
			debug3("gang: _timeslicer_thread: part %s: "
			       "run %u total %u", p_ptr->part_name, 
			       p_ptr->jobs_active, p_ptr->num_jobs);
			if (p_ptr->jobs_active <
					p_ptr->num_jobs + p_ptr->num_shadows)
				_cycle_job_list(p_ptr);
		}
		pthread_mutex_unlock(&data_mutex);

		/* Preempt jobs that were formerly only suspended */
		_preempt_job_dequeue();	/* MUST BE OUTSIDE data_mutex lock */

		/* sleep AND check for thread termination requests */
		pthread_testcancel();
		debug3("gang: _timeslicer_thread: preparing to sleep");
		sleep(timeslicer_seconds);
		debug3("gang: _timeslicer_thread: waking up");
		pthread_testcancel();
	}
	pthread_exit((void *) 0);
	return NULL;
}
