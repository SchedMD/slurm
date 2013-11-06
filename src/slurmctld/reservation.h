/*****************************************************************************\
 *  reservation.h - resource reservation management
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef _RESERVATION_H
#define _RESERVATION_H

#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "src/common/bitstring.h"
#include "src/slurmctld/slurmctld.h"

extern time_t last_resv_update;

/* Create a resource reservation */
extern int create_resv(resv_desc_msg_t *resv_desc_ptr);

/* Update an existing resource reservation */
extern int update_resv(resv_desc_msg_t *resv_desc_ptr);

/* Delete an existing resource reservation */
extern int delete_resv(reservation_name_msg_t *resv_desc_ptr);

/* Return pointer to the named reservation or NULL if not found */
extern slurmctld_resv_t *find_resv_name(char *resv_name);

/* Dump the reservation records to a buffer */
extern void show_resv(char **buffer_ptr, int *buffer_size, uid_t uid,
		      uint16_t protocol_version);

/* Save the state of all reservations to file */
extern int dump_all_resv_state(void);

/* Purge all reservation data structures */
extern void resv_fini(void);

/* send all reservations to accounting.  Only needed at
 * first registration
 */
extern int send_resvs_to_accounting(void);

/* Set or clear NODE_STATE_MAINT for node_state as needed
 * IN reset_all - re-initialize all node information for all reservations
 * RET count of newly started reservations
 */
extern int set_node_maint_mode(bool reset_all);

/* checks if node within node_record_table_ptr is in maint reservation */
extern bool is_node_in_maint_reservation(int nodenum);

/* After an assocation has been added or removed update the lists. */
extern void update_assocs_in_resvs(void);

/*
 * Update reserved nodes for all reservations using a specific partition if the
 * resevation has NodeList=ALL and RESERVE_FLAGS_PART_NODES.
*/
extern void update_part_nodes_in_resv(struct part_record *part_ptr);

/*
 * Load the reservation state from file, recover on slurmctld restart.
 *	Reset reservation pointers for all jobs.
 *	Execute this after loading the configuration file data.
 * IN recover - 0 = validate current reservations ONLY if already recovered,
 *                  otherwise recover from disk
 *              1+ = recover all reservation state from disk
 * RET SLURM_SUCCESS or error code
 * NOTE: READ lock_slurmctld config before entry
 */
extern int load_all_resv_state(int recover);

/*
 * Determine if a job request can use the specified reservations
 *
 * IN/OUT job_ptr - job to validate, set its resv_id and resv_type
 * RET SLURM_SUCCESS or error code (not found or access denied)
 */
extern int validate_job_resv(struct job_record *job_ptr);

/*
 * Determine how many licenses of the give type the specified job is
 *	prevented from using due to reservations
 *
 * IN job_ptr   - job to test
 * IN lic_name  - name of license
 * IN when      - when the job is expected to start
 * RET number of licenses of this type the job is prevented from using
 */
extern int job_test_lic_resv(struct job_record *job_ptr, char *lic_name,
			     time_t when);

/*
 * Determine which nodes a job can use based upon reservations
 *
 * IN job_ptr      - job to test
 * IN/OUT when     - when we want the job to start (IN)
 *                   when the reservation is available (OUT)
 * IN move_time    - if true, then permit the start time to advance from
 *                   "when" as needed IF job has no reservervation
 * OUT node_bitmap - nodes which the job can use, caller must free
 * OUT exc_core_bitmap - cores which the job can NOT use, caller must free
 * RET	SLURM_SUCCESS if runable now
 *	ESLURM_RESERVATION_ACCESS access to reservation denied
 *	ESLURM_RESERVATION_INVALID reservation invalid
 *	ESLURM_INVALID_TIME_VALUE reservation invalid at time "when"
 *	ESLURM_NODES_BUSY job has no reservation, but required nodes are
 *			  reserved
 */
extern int job_test_resv(struct job_record *job_ptr, time_t *when,
			 bool move_time, bitstr_t **node_bitmap,
			 bitstr_t **exc_core_bitmap);

/*
 * Determine the time of the first reservation to end after some time.
 * return zero of no reservation ends after that time.
 * IN start_time - look for reservations ending after this time
 * RET the reservation end time or zero of none found
 */
extern time_t find_resv_end(time_t start_time);

/*
 * Determine if a job can start now based only upon its reservations
 *	specification, if any
 *
 * IN job_ptr      - job to test
 * RET	SLURM_SUCCESS if runable now, otherwise an error code
 */
extern int job_test_resv_now(struct job_record *job_ptr);

/* Adjust a job's time_limit and end_time as needed to avoid using
 *	reserved resources. Don't go below job's time_min value. */
extern void job_time_adj_resv(struct job_record *job_ptr);

/* Begin scan of all jobs for valid reservations */
extern void begin_job_resv_check(void);

/* Test a particular job for valid reservation
 *
 * RET ESLURM_INVALID_TIME_VALUE if reservation is terminated
 *     SLURM_SUCCESS if reservation is still valid
 */
extern int job_resv_check(struct job_record *job_ptr);

/* Finish scan of all jobs for valid reservations
 *
 * Purge vestigial reservation records.
 * Advance daily or weekly reservations that are no longer
 *	being actively used.
 */
extern void fini_job_resv_check(void);

#endif /* !_RESERVATION_H */
