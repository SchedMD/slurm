/*****************************************************************************\
 *  select_linear.c - node selection plugin for simple one-dimensional 
 *  address space. Selects nodes for a job so as to minimize the number 
 *  of sets of consecutive nodes using a best-fit algorithm.
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  endif
#endif

#include <stdio.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/slurmctld/slurmctld.h"

#define SELECT_DEBUG 0

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
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "select" for SLURM node selection) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load select plugins if the plugin_type string has a 
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the node selection API matures.
 */
const char plugin_name[]       	= "Linear node selection plugin";
const char plugin_type[]       	= "select/linear";
const uint32_t plugin_version	= 90;

static struct node_record *select_node_ptr;
static int select_node_cnt;
static uint16_t select_fast_schedule;

static bool 
_enough_nodes(int avail_nodes, int rem_nodes, int min_nodes, int max_nodes)
{
	int needed_nodes;

	if (max_nodes)
		needed_nodes = rem_nodes + min_nodes - max_nodes;
	else
		needed_nodes = rem_nodes;

	return(avail_nodes >= needed_nodes);
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard SLURM 
 * node selection API.
 */

extern int select_p_state_save(char *dir_name)
{
	return SLURM_SUCCESS;
}

extern int select_p_state_restore(char *dir_name)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_init(List job_list)
{
	return SLURM_SUCCESS;
}

extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
	if (node_ptr == NULL) {
		error("select_p_node_init: node_ptr == NULL");
		return SLURM_ERROR;
	}

	if (node_cnt < 0) {
		error("select_p_node_init: node_cnt < 0");
		return SLURM_ERROR;
	}

	select_node_ptr = node_ptr;
	select_node_cnt = node_cnt;
	select_fast_schedule = slurm_get_fast_schedule();

	return SLURM_SUCCESS;
}

extern int select_p_part_init(List part_list)
{
	return SLURM_SUCCESS;
}

/*
 * select_p_job_test - Given a specification of scheduling requirements, 
 *	identify the nodes which "best" satisfy the request.
 * 	"best" is defined as either single set of consecutive nodes satisfying 
 *	the request and leaving the minimum number of unused nodes OR 
 *	the fewest number of consecutive node sets
 * IN job_ptr - pointer to job being scheduled
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to 
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * RET zero on success, EINVAL otherwise
 * globals (passed via select_p_node_init): 
 *	node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: the job information that is considered for scheduling includes:
 *	req_node_bitmap: bitmap of specific nodes required by the job
 *	contiguous: allocated nodes must be sequentially located
 *	num_procs: minimum number of processors required by the job
 * NOTE: bitmap must be a superset of req_nodes at the time that 
 *	select_p_job_test is called
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
		int min_nodes, int max_nodes)
{
	int i, index, error_code = EINVAL, sufficient;
	int *consec_nodes;	/* how many nodes we can add from this 
				 * consecutive set of nodes */
	int *consec_cpus;	/* how many nodes we can add from this 
				 * consecutive set of nodes */
	int *consec_start;	/* where this consecutive set starts (index) */
	int *consec_end;	/* where this consecutive set ends (index) */
	int *consec_req;	/* are nodes from this set required 
				 * (in req_bitmap) */
	int consec_index, consec_size;
	int rem_cpus, rem_nodes;	/* remaining resources required */
	int best_fit_nodes, best_fit_cpus, best_fit_req;
	int best_fit_location = 0, best_fit_sufficient;

	xassert(bitmap);

	consec_index = 0;
	consec_size  = 50;	/* start allocation for 50 sets of 
				 * consecutive nodes */
	consec_cpus  = xmalloc(sizeof(int) * consec_size);
	consec_nodes = xmalloc(sizeof(int) * consec_size);
	consec_start = xmalloc(sizeof(int) * consec_size);
	consec_end   = xmalloc(sizeof(int) * consec_size);
	consec_req   = xmalloc(sizeof(int) * consec_size);

	/* Build table with information about sets of consecutive nodes */
	consec_cpus[consec_index] = consec_nodes[consec_index] = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */
	rem_cpus = job_ptr->num_procs;
	if (max_nodes)
		rem_nodes = max_nodes;
	else
		rem_nodes = min_nodes;
	for (index = 0; index < select_node_cnt; index++) {
		if (bit_test(bitmap, index)) {
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = index;
			if (select_fast_schedule)
				/* don't bother checking each node */
				i = select_node_ptr[index].
				    config_ptr->cpus;
			else
				i = select_node_ptr[index].cpus;
			if (job_ptr->details->req_node_bitmap && 
			    bit_test(job_ptr->details->req_node_bitmap, index)) {
				if (consec_req[consec_index] == -1)
					/* first required node in set */
					consec_req[consec_index] = index;
				rem_cpus -= i;
				rem_nodes--;
			} else {	 /* node not required (yet) */
				bit_clear(bitmap, index); 
				consec_cpus[consec_index] += i;
				consec_nodes[consec_index]++;
			}
		} else if (consec_nodes[consec_index] == 0) {
			consec_req[consec_index] = -1;
			/* already picked up any required nodes */
			/* re-use this record */
		} else {
			consec_end[consec_index] = index - 1;
			if (++consec_index >= consec_size) {
				consec_size *= 2;
				xrealloc(consec_cpus,
					 sizeof(int) * consec_size);
				xrealloc(consec_nodes,
					 sizeof(int) * consec_size);
				xrealloc(consec_start,
					 sizeof(int) * consec_size);
				xrealloc(consec_end,
					 sizeof(int) * consec_size);
				xrealloc(consec_req,
					 sizeof(int) * consec_size);
			}
			consec_cpus[consec_index] = 0;
			consec_nodes[consec_index] = 0;
			consec_req[consec_index] = -1;
		}
	}
	if (consec_nodes[consec_index] != 0)
		consec_end[consec_index++] = index - 1;

#if SELECT_DEBUG
	/* don't compile this, slows things down too much */
	debug3("rem_cpus=%d, rem_nodes=%d", rem_cpus, rem_nodes);
	for (i = 0; i < consec_index; i++) {
		if (consec_req[i] != -1)
			debug3
			    ("start=%s, end=%s, nodes=%d, cpus=%d, req=%s",
			     select_node_ptr[consec_start[i]].name,
			     select_node_ptr[consec_end[i]].name,
			     consec_nodes[i], consec_cpus[i],
			     select_node_ptr[consec_req[i]].name);
		else
			debug3("start=%s, end=%s, nodes=%d, cpus=%d",
			       select_node_ptr[consec_start[i]].name,
			       select_node_ptr[consec_end[i]].name,
			       consec_nodes[i], consec_cpus[i]);
	}
#endif

	/* accumulate nodes from these sets of consecutive nodes until */
	/*   sufficient resources have been accumulated */
	while (consec_index) {
		best_fit_cpus = best_fit_nodes = best_fit_sufficient = 0;
		best_fit_req = -1;	/* first required node, -1 if none */
		for (i = 0; i < consec_index; i++) {
			if (consec_nodes[i] == 0)
				continue;
			sufficient = ((consec_nodes[i] >= rem_nodes)
				      && (consec_cpus[i] >= rem_cpus));

			/* if first possibility OR */
			/* contains required nodes OR */
			/* first set large enough for request OR */
			/* tightest fit (less resource waste) OR */
			/* nothing yet large enough, but this is biggest */
			if ((best_fit_nodes == 0) ||	
			    ((best_fit_req == -1) && (consec_req[i] != -1)) ||
			    (sufficient && (best_fit_sufficient == 0)) ||
			    (sufficient && (consec_cpus[i] < best_fit_cpus)) ||	
			    ((sufficient == 0) && 
			     (consec_cpus[i] > best_fit_cpus))) {
				best_fit_cpus = consec_cpus[i];
				best_fit_nodes = consec_nodes[i];
				best_fit_location = i;
				best_fit_req = consec_req[i];
				best_fit_sufficient = sufficient;
			}
		}
		if (best_fit_nodes == 0)
			break;
		if (job_ptr->details->contiguous && 
		    ((best_fit_cpus < rem_cpus) ||
		     (!_enough_nodes(best_fit_nodes, rem_nodes, 
				     min_nodes, max_nodes))))
			break;	/* no hole large enough */
		if (best_fit_req != -1) {
			/* This collection of nodes includes required ones
			 * select nodes from this set, first working up
			 * then down from the required nodes */
			for (i = best_fit_req;
			     i <= consec_end[best_fit_location]; i++) {
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				if (bit_test(bitmap, i))
					continue;
				bit_set(bitmap, i);
				rem_nodes--;
				if (select_fast_schedule)
					rem_cpus -= select_node_ptr[i].
							config_ptr->cpus;
				else
					rem_cpus -= select_node_ptr[i].
							cpus;
			}
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_location]; i--) {
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				/* if (bit_test(bitmap, i)) 
					continue;  cleared above earlier */
				bit_set(bitmap, i);
				rem_nodes--;
				if (select_fast_schedule)
					rem_cpus -= select_node_ptr[i].
							config_ptr->cpus;
				else
					rem_cpus -= select_node_ptr[i].
							cpus;
			}
		} else {
			for (i = consec_start[best_fit_location];
			     i <= consec_end[best_fit_location]; i++) {
				if ((rem_nodes <= 0) && (rem_cpus <= 0))
					break;
				if (bit_test(bitmap, i))
					continue;
				bit_set(bitmap, i);
				rem_nodes--;
				if (select_fast_schedule)
					rem_cpus -= select_node_ptr[i].
							config_ptr->cpus;
				else
					rem_cpus -= select_node_ptr[i].
							cpus;
			}
		}
		if (job_ptr->details->contiguous || 
		    ((rem_nodes <= 0) && (rem_cpus <= 0))) {
			error_code = SLURM_SUCCESS;
			break;
		}
		consec_cpus[best_fit_location] = 0;
		consec_nodes[best_fit_location] = 0;
	}

	if (error_code && (rem_cpus <= 0) && 
	    ((max_nodes == 0) || ((max_nodes - rem_nodes) >= min_nodes)))
		error_code = SLURM_SUCCESS;

	xfree(consec_cpus);
	xfree(consec_nodes);
	xfree(consec_start);
	xfree(consec_end);
	xfree(consec_req);
	return error_code;
}

extern int select_p_job_begin(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_ready(struct job_record *job_ptr)
{
	if (job_ptr->job_state != JOB_RUNNING)
		return 0;

	return 1;
}

extern int select_p_pack_node_info(time_t last_query_time, Buf *buffer_ptr)
{
	/* This function is always invalid on normal Linux clusters */
	return SLURM_ERROR;
}

