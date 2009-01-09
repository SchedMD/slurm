/*****************************************************************************\
 *  basil_interface.c - slurmctld interface to BASIL, Cray's Batch Application
 *	Scheduler Interface Layer (BASIL). In order to support development, 
 *	these functions will provide basic BASIL-like functionality even 
 *	without a BASIL command being present.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

/* FIXME: In slurmctld/node_mgr.c, make _sync_bitmaps() extern */
/* FIXME: Document, ALPS must be started before SLURM */
/* FIXME: Document BASIL_RESERVATION_ID env var */

#if HAVE_CONFIG_H
#  include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <slurm/slurm_errno.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/basil_interface.h"
#include "src/slurmctld/slurmctld.h"

#define BASIL_DEBUG 1

#ifndef APBASIL_LOC
static int last_res_id = 0;
#endif	/* !APBASIL_LOC */

#ifdef HAVE_CRAY_XT
#ifdef APBASIL_LOC
/* Make sure that each SLURM node has a BASIL node ID */
static void _validate_basil_node_id(void)
{
	uint16_t base_state;
	int i;
	struct node_record *node_ptr = node_record_table_ptr;

	for (i=0; i<node_record_cnt; i++, node_ptr++)
		if (node_ptr->basil_node_id != NO_VAL)
			continue;
		base_state = node_ptr->state & NODE_STATE_BASE;
		if (base_state == NODE_STATE_DOWN)
			continue;

		error("Node %s has no basil node_id", node_ptr->name);
		last_node_update = time(NULL);
		set_node_down(node_ptr->name, "No BASIL node_id");
		_sync_bitmaps(node_ptr, 0);
	}
}
#endif	/* APBASIL_LOC */
#endif	/* HAVE_CRAY_XT */

/*
 * basil_query - Query BASIL for node and reservation state.
 * Execute once at slurmctld startup and periodically thereafter.
 * RET 0 or error code
 */
extern int basil_query(void)
{
	int error_code = SLURM_SUCCESS;
#ifdef HAVE_CRAY_XT
#ifdef APBASIL_LOC
	struct config_record *config_ptr;
	struct node_record *node_ptr;
	struct job_record *job_ptr;
	ListIterator job_iterator;
	uint16_t base_state;
	int i;
	char *reason, *res_id;
	static bool first_run = true;

	/* Issue the BASIL QUERY request */
	if (request_failure) {
		fatal("basil query error: %s", "TBD");
		return SLURM_ERROR;
	}
	debug("basil query initiated");

	if (first_run) {
		/* Set basil_node_id to NO_VAL since the default value 
		 * of zero is a valid BASIL node ID */
		node_ptr = node_record_table_ptr;
		for (i=0; i<node_record_cnt; i++, node_ptr++)
			node_ptr->basil_node_id = NO_VAL;
		first_run = false;
	}

	/* Validate configuration for each node that BASIL reports */
	for (each_basil_node) {
#if BASIL_DEBUG
		/* Log node state according to BASIL */
		info("basil query: name=%s arch=%s",
		     basil_node_name, basil_node_arch, etc.);
#endif	/* BASIL_DEBUG */

		/* NOTE: Cray should provide X-, Y- and Z-coordinates
		 * in the future. When that happens, we'll want to use
		 * those numbers to generate the hostname:
		 * slurm_host_name = xmalloc(sizeof(conf->node_prefix) + 4);
		 * sprintf(slurm_host_name: %s%d%d%d", basil_node_name, X,Y,Z);
		 * Until then the node name must contain a 3-digit numberic
		 * suffix specifying the X-, Y- and Z-coordinates.
		 */
		node_ptr = find_node_record(basil_node_name);
		if (node_ptr == NULL) {
			error("basil node %s not found in slurm",
			      basil_node_name);
			continue;
		}

		/* Record BASIL's node_id for use in reservations */
		node_ptr->basil_node_id = basil_node_id;

		/* Update architecture in slurmctld's node record */
		if (node_ptr->arch == NULL) {
			xfree(node_ptr->arch);
			node_ptr->arch = xstrdup(basil_node_arch);
		}

		/* Update slurmctld's node state if necessary */
		reason = NULL;
		base_state = node_ptr->state & NODE_STATE_BASE;
		if (base_state != NODE_STATE_DOWN) {
			if (strcmp(basil_state, "UP"))
				reason = "basil state not UP";
			else if (strcmp(basil_role, "BATCH"))
				reason = "basil role not BATCH";
		}

		/* Calculate the total count of processors and 
		 * MB of memory on the node */
		config_ptr = node_ptr->config_ptr;
		if ((slurmctld_conf.fast_schedule != 2) &&
		    (basil_cpus < config_ptr->cpus)) {
			error("Node %s has low cpu count %d",
 			      node_ptr->name, basil_cpus);
			reason = "Low CPUs";
		}
		node_ptr->cpus = basil_cpus;
		if ((slurmctld_conf.fast_schedule != 2) &&
		    (basil_memory < config_ptr->real_memory)) {
			error("Node %s has low real_memory size %d",
			     node_ptr->name, basil_memory);
			reason = "Low RealMemory";
		}
		node_ptr->real_memory = basil_memory;

		if (reason) {
			last_node_update = time(NULL);
			set_node_down(node_ptr->name, reason);
			_sync_bitmaps(node_ptr, 0);
		}
	}
	_validate_basil_node_id();

	/* Confirm that each BASIL reservation is still valid, 
	 * purge vestigial reservations */
	for (each_basil_reservation) {
		bool found = false;
		job_iterator = list_iterator_create(job_list);
		while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
			select_g_get_jobinfo(job_ptr->select_jobinfo, 
					     SELECT_DATA_RESV_ID, &res_id);
			found = !strcmp(res_id, basil_reservation_id);
			xfree(res_id);
			if (found)
				break;
		}
		list_iterator_destroy(job_iterator);
		if (found) {
			error("vestigial basil reservation %s being removed",
			      basil_reservation_id);
			basil_dealloc(basil_reservation_id);
		}
	}
#else
	struct job_record *job_ptr;
	ListIterator job_iterator;
	char *res_id, *tmp;
	int job_res_id;

	/* Capture the highest reservation ID recorded to avoid re-use */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		res_id = NULL;
		select_g_get_jobinfo(job_ptr->select_jobinfo, 
				     SELECT_DATA_RESV_ID, &res_id);
		if (res_id) {
			tmp = strchr(res_id, '_');
			if (tmp) {
				job_res_id = atoi(tmp+1);
				last_res_id = MAX(last_res_id, job_res_id);
			}
			xfree(res_id);
		}
	}
	list_iterator_destroy(job_iterator);
	debug("basil_query() executed, last_res_id=%d", last_res_id);
#endif	/* APBASIL_LOC */
#endif	/* HAVE_CRAY_XT */

	return error_code;
}

/*
 * basil_reserve - create a BASIL reservation.
 * IN job_ptr - pointer to job which has just been allocated resources
 * RET 0 or error code, job will abort or be requeued on failure
 */
extern int basil_reserve(struct job_record *job_ptr)
{
	int error_code = SLURM_SUCCESS;
#ifdef HAVE_CRAY_XT
#ifdef APBASIL_LOC
	/* Issue the BASIL RESERVE request */
	if (request_failure) {
		error("basil reserve error: %s", "TBD");
		return SLURM_ERROR;
	}
	select_g_set_jobinfo(job_ptr->select_jobinfo, 
			     SELECT_DATA_RESV_ID, reservation_id);
	debug("basil reservation made job_id=%u resv_id=%s", 
	      job_ptr->job_id, reservation_id);
#else
	char reservation_id[32];
	snprintf(reservation_id, sizeof(reservation_id), 
		"resv_%d", ++last_res_id);
	select_g_set_jobinfo(job_ptr->select_jobinfo, 
			     SELECT_DATA_RESV_ID, reservation_id);
	debug("basil reservation made job_id=%u resv_id=%s", 
	      job_ptr->job_id, reservation_id);
#endif	/* APBASIL_LOC */
#endif	/* HAVE_CRAY_XT */
	return error_code;
}

/*
 * basil_release - release a BASIL reservation by job.
 * IN job_ptr - pointer to job which has just been deallocated resources
 * RET 0 or error code
 */
extern int basil_release(struct job_record *job_ptr)
{
	int error_code = SLURM_SUCCESS;
#ifdef HAVE_CRAY_XT
	char *reservation_id = NULL;
	select_g_get_jobinfo(job_ptr->select_jobinfo, 
			     SELECT_DATA_RESV_ID, &reservation_id);
	if (reservation_id) {
		error_code = basil_release_id(reservation_id);
		xfree(reservation_id);
	}
#endif	/* HAVE_CRAY_XT */
	return error_code;
}

/*
 * basil_release_id - release a BASIL reservation by ID.
 * IN reservation_id - ID of reservation to release
 * RET 0 or error code
 */
extern int basil_release_id(char *reservation_id)
{
	int error_code = SLURM_SUCCESS;
#ifdef HAVE_CRAY_XT
#ifdef APBASIL_LOC
	/* Issue the BASIL RELEASE request */
	if (request_failure) {
		error("basil release of %s error: %s", reservation_id, "TBD");
		return SLURM_ERROR;
	}
	debug("basil release of reservation %s complete", reservation_id);
#else
	debug("basil release of reservation %s complete", reservation_id);
#endif	/* APBASIL_LOC */
#endif	/* HAVE_CRAY_XT */
	return error_code;
}
