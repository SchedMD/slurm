/*****************************************************************************\
 *  reservation.h - resource reservation management
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#ifndef _RESERVATION_H
#define _RESERVATION_H

#include <time.h>
#include <unistd.h>
#include <slurm/slurm.h>
#include "src/common/bitstring.h"
#include "src/slurmctld/slurmctld.h"

extern time_t last_resv_update;

/* Create a resource reservation */
extern int create_resv(reserve_request_msg_t *resv_desc_ptr);

/* Update an exiting resource reservation */
extern int update_resv(reserve_request_msg_t *resv_desc_ptr);

/* Delete an exiting resource reservation */
extern int delete_resv(reservation_name_msg_t *resv_desc_ptr);

/* Dump the reservation records to a buffer */
extern void show_resv(char **buffer_ptr, int *buffer_size, uid_t uid);

/* Save the state of all reservations to file */
extern int dump_all_resv_state(void);

/* Purge all reservation data structures */
extern void resv_fini(void);

/* send all reservations to accounting.  Only needed at
 * first registration
 */
extern int send_resvs_to_accounting();

/* Set or clear NODE_STATE_MAINT for node_state as needed */
extern void set_node_maint_mode(void);

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
 * IN/OUT job_ptr - job to validate, set its resv_id and resv_type
 * RET SLURM_SUCCESS or error code (not found or access denied)
*/
extern int validate_job_resv(struct job_record *job_ptr);

/*
 * Determine which nodes a job can use based upon reservations
 * IN job_ptr      - job to test
 * IN/OUT when     - when we want the job to start (IN)
 *                   when the reservation is available (OUT)
 * OUT node_bitmap - nodes which the job can use, caller must free
 * RET	SLURM_SUCCESS if runable now
 *	ESLURM_RESERVATION_ACCESS access to reservation denied
 *	ESLURM_RESERVATION_INVALID reservation invalid
 *	ESLURM_INVALID_TIME_VALUE reservation invalid at time "when"
 */
extern int job_test_resv(struct job_record *job_ptr, time_t *when,
			 bitstr_t **node_bitmap);

/*
 * Determine if a job can start now based only upon reservations
 * IN job_ptr      - job to test
 * RET	SLURM_SUCCESS if runable now, otherwise an error code
 */
extern int job_test_resv_now(struct job_record *job_ptr);

/* Begin scan of all jobs for valid reservations */
extern void begin_job_resv_check(void);

/* Test a particular job for valid reservation
 * RET ESLURM_INVALID_TIME_VALUE if reservation is terminated
 *     SLURM_SUCCESS if reservation is still valid */
extern int job_resv_check(struct job_record *job_ptr);

/* Finish scan of all jobs for valid reservations */
extern void fini_job_resv_check(void);

#endif /* !_RESERVATION_H */
