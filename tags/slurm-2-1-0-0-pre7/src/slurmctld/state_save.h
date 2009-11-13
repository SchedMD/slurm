/*****************************************************************************\
 *  state_save.h - Definitions for keeping saved slurmctld state current 
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
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

#ifndef _SLURMCTLD_STATE_SAVE_H
#define _SLURMCTLD_STATE_SAVE_H

/* fsync() and close() a file, 
 * Execute fsync() and close() multiple times if necessary and log failures
 * RET 0 on success or -1 on error */
extern int fsync_and_close(int fd, char *file_type);

/* Queue saving of job state information */
extern void schedule_job_save(void);

/* Queue saving of node state information */
extern void schedule_node_save(void);

/* Queue saving of partition state information */
extern void schedule_part_save(void);

/* Queue saving of reservation state information */
extern void schedule_resv_save(void);

/* Queue saving of trigger state information */
extern void schedule_trigger_save(void);

/* shutdown the slurmctld_state_save thread */
extern void shutdown_state_save(void);

/*
 * Run as pthread to keep saving slurmctld state information as needed,
 * Use schedule_job_save(),  schedule_node_save(), schedule_part_save(),
 * schedule_trigger_save() to queue state save of each data structure 
 * no_data IN - unused
 * RET - NULL
 */
extern void *slurmctld_state_save(void *no_data);

#endif
