/*****************************************************************************\
 *  node_scheduler.h - definitions of functions in node_scheduler.c
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#ifndef _HAVE_NODE_SCHEDULER_H
#define _HAVE_NODE_SCHEDULER_H

/*
 * allocate_nodes - change state of specified nodes to NODE_STATE_ALLOCATED
 *	also claim required licenses
 * IN job_ptr - job being allocated resources
 */
extern void allocate_nodes(struct job_record *job_ptr);

/* For a given job, if the available nodes differ from those with currently
 *	active features, return a bitmap of nodes with the job's required
 *	features currently active
 * IN job_ptr - job requesting resource allocation
 * IN avail_bitmap - nodes currently available for this job
 * OUT active_bitmap - nodes with job's features currently active, NULL if
 *	identical to avail_bitmap
 * NOTE: Currently supports only simple AND of features
 */
extern void build_active_feature_bitmap(struct job_record *job_ptr,
					bitstr_t *avail_bitmap,
					bitstr_t **active_bitmap);

/*
 * build_node_details - sets addresses for allocated nodes
 * IN job_ptr - pointer to a job record
 * IN new_alloc - set if new job allocation, cleared if state recovery
 */
extern void build_node_details(struct job_record *job_ptr, bool new_alloc);

/*
 * deallocate_nodes - for a given job, deallocate its nodes and make
 *	their state NODE_STATE_COMPLETING
 *	also release the job's licenses
 * IN job_ptr - pointer to terminating job (already in some COMPLETING state)
 * IN timeout - true if job exhausted time limit, send REQUEST_KILL_TIMELIMIT
 *	RPC instead of REQUEST_TERMINATE_JOB
 * IN suspended - true if job was already suspended (node's run_job_cnt
 *	already decremented);
 * IN preempted - true if job is being preempted
 */
extern void deallocate_nodes(struct job_record *job_ptr, bool timeout,
		bool suspended, bool preempted);

/* Remove nodes from consideration for allocation based upon "mcs" by
 * other users
 * job_ptr IN - Job to be scheduled
 * usable_node_mask IN/OUT - Nodes available for use by this job's mcs
 */
extern void filter_by_node_mcs(struct job_record *job_ptr, int mcs_select,
			       bitstr_t *usable_node_mask);

/* Remove nodes from consideration for allocation based upon "ownership" by
 * other users
 * job_ptr IN - Job to be scheduled
 * usable_node_mask IN/OUT - Nodes available for use by this job's user
 */
extern void filter_by_node_owner(struct job_record *job_ptr,
				 bitstr_t *usable_node_mask);

/*
 * re_kill_job - for a given job, deallocate its nodes for a second time,
 *	basically a cleanup for failed deallocate() calls
 * IN job_ptr - pointer to terminating job (already in some COMPLETING state)
 * globals: node_record_count - number of nodes in the system
 *	node_record_table_ptr - pointer to global node table
 */
extern void re_kill_job(struct job_record *job_ptr);

/*
 * select_nodes - select and allocate nodes to a specific job
 * IN job_ptr - pointer to the job record
 * IN test_only - if set do not allocate nodes, just confirm they
 *	could be allocated now
 * IN select_node_bitmap - bitmap of nodes to be used for the
 *	job's resource allocation (not returned if NULL), caller
 *	must free
 * IN submission - if set ignore reservations
 * OUT err_msg - if not NULL set to error message for job, caller must xfree
 * RET 0 on success, ESLURM code from slurm_errno.h otherwise
 * globals: list_part - global list of partition info
 *	default_part_loc - pointer to default partition
 *	config_list - global list of node configuration info
 * Notes: The algorithm is
 *	1) Build a table (node_set_ptr) of nodes with the requisite
 *	   configuration. Each table entry includes their weight,
 *	   node_list, features, etc.
 *	2) Call _pick_best_nodes() to select those nodes best satisfying
 *	   the request, (e.g. best-fit or other criterion)
 *	3) Call allocate_nodes() to perform the actual allocation
 */
extern int select_nodes(struct job_record *job_ptr, bool test_only,
			bitstr_t **select_node_bitmap, char **err_msg,
			bool submission);

/*
 * get_node_cnts - determine the number of nodes for the requested job.
 * IN job_ptr - pointer to the job record.
 * IN qos_flags - Flags of the job_ptr's qos.  This is so we don't have to send
 *                in a pointer or lock the qos read lock before calling.
 * IN part_ptr - pointer to the job's partition.
 * OUT min_nodes - The minimum number of nodes for the job.
 * OUT req_nodes - The number of node the select plugin should target.
 * OUT max_nodes - The max number of nodes for the job.
 * RET SLURM_SUCCESS on success, ESLURM code from slurm_errno.h otherwise.
 */
extern int get_node_cnts(struct job_record *job_ptr,
			 uint32_t qos_flags,
			 struct part_record *part_ptr,
			 uint32_t *min_nodes,
			 uint32_t *req_nodes, uint32_t *max_nodes);

/* launch_prolog - launch job prolog script by slurmd on allocated nodes
 * IN job_ptr - pointer to the job record
 */
extern void launch_prolog(struct job_record *job_ptr);

#endif /* !_HAVE_NODE_SCHEDULER_H */
