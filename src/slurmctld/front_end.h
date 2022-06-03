/*****************************************************************************\
 *  front_end.h - Define front end node functions.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#ifndef __SLURM_FRONT_END_H__
#define __SLURM_FRONT_END_H__

#include "src/slurmctld/slurmctld.h"

/*
 * assign_front_end - assign a front end node for starting a job
 * job_ptr IN - job to assign a front end node (tests access control lists)
 * RET pointer to the front end node to use or NULL if none found
 */
extern front_end_record_t *assign_front_end(job_record_t *job_ptr);

/*
 * avail_front_end - test if any front end nodes are available for starting job
 * job_ptr IN - job to consider for starting (tests access control lists) or
 *              NULL to test if any job can start (no test of ACL)
 */
extern bool avail_front_end(job_record_t *job_ptr);

/* dump_all_front_end_state - save the state of all front_end nodes to file */
extern int dump_all_front_end_state(void);

/*
 * find_front_end_record - find a record for front_endnode with specified name
 * input: name - name of the desired front_end node
 * output: return pointer to front_end node record or NULL if not found
 */
extern front_end_record_t *find_front_end_record(char *name);

/*
 * load_all_front_end_state - Load the front_end node state from file, recover
 *	on slurmctld restart. Execute this after loading the configuration
 *	file data. Data goes into common storage.
 * IN state_only - if true, overwrite only front_end node state and reason
 *	Use this to overwrite the "UNKNOWN state typically used in slurm.conf
 * RET 0 or error code
 * NOTE: READ lock_slurmctld config before entry
 */
extern int load_all_front_end_state(bool state_only);

/*
 * log_front_end_state - log all front end node state
 */
extern void log_front_end_state(void);

/*
 * pack_all_front_end - dump all front_end node information for all nodes
 *	in machine independent form (for network transmission)
 * OUT buffer_ptr - pointer to the stored data
 * OUT buffer_size - set to size of the buffer in bytes
 * IN protocol_version - slurm protocol version of client
 * NOTE: the caller must xfree the buffer at *buffer_ptr
 * NOTE: READ lock_slurmctld config before entry
 */
extern void pack_all_front_end(char **buffer_ptr, int *buffer_size, uid_t uid,
			       uint16_t protocol_version);

/*
 * purge_front_end_state - purge all front end node state
 */
extern void purge_front_end_state(void);

/*
 * restore_front_end_state - restore front end node state
 * IN recover - replace job, node and/or partition data with latest
 *              available information depending upon value
 *              0 = use no saved state information, rebuild everything from
 *		    slurm.conf contents
 *              1 = recover saved job and trigger state,
 *                  node DOWN/DRAIN/FAIL state and reason information
 *              2 = recover all saved state
 */
extern void restore_front_end_state(int recover);

/*
 * set_front_end_down - make the specified front end node's state DOWN and
 *	kill jobs as needed
 * IN front_end_pt - pointer to the front end node
 * IN reason - why the node is DOWN
 */
extern void set_front_end_down (front_end_record_t *front_end_ptr,
				char *reason);

/*
 * sync_front_end_state - synchronize job pointers and front-end node state
 */
extern void sync_front_end_state(void);

/*
 * Update front end node state
 * update_front_end_msg_ptr IN change specification
 * IN auth_uid - UID that issued the update
 * RET SLURM_SUCCESS or error code
 */
extern int update_front_end(update_front_end_msg_t *update_front_end_msg_ptr,
			    uid_t auth_uid);

#endif /*__SLURM_FRONT_END_H__*/
