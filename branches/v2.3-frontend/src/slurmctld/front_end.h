/*****************************************************************************\
 *  front_end.h - Define front end node functions.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#ifndef __SLURM_FRONT_END_H__
#define __SLURM_FRONT_END_H__

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

#endif /*__SLURM_FRONT_END_H__*/
