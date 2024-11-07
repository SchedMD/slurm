/*****************************************************************************\
 *  oracle.h - Infrastructure for the bf_topopt/"oracle" subsystem
 *
 *  The oracle() function controls job start delays based on
 *  fragmentation costs, optimizing job placement for efficiency.
 *
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#ifndef _SLURM_ORACLE_H
#define _SLURM_ORACLE_H

#define MAX_ORACLE_DEPTH 30
#define ORACLE_DEPTH 10

typedef struct bf_slot {
	time_t start;
	bitstr_t *job_bitmap;
	bitstr_t *job_mask;
	bitstr_t *cluster_bitmap;
	uint32_t time_limit;
	uint32_t boot_time;
	uint32_t job_score;
	uint32_t cluster_score;
} bf_slot_t;

extern int bf_topopt_iterations;
extern int used_slots;

void init_oracle(void);

void fini_oracle(void);

bool oracle(job_record_t *job_ptr, bitstr_t *job_bitmap, time_t later_start,
	    uint32_t *time_limit, uint32_t *boot_time,
	    node_space_map_t *node_space);

#endif /* _SLURM_ORACLE_H */
