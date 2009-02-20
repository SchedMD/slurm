/*****************************************************************************\
 *  basil_interface.h - slurmctld interface to BASIL, Cray's Batch Application
 *	Scheduler Interface Layer (BASIL)
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
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

#ifndef _HAVE_BASIL_INTERFACE_H
#define _HAVE_BASIL_INTERFACE_H

#include "src/slurmctld/slurmctld.h"

/*
 * basil_query - Query BASIL for node and reservation state.
 * Execute once at slurmctld startup and periodically thereafter.
 * RET 0 or error code
 */
extern int basil_query(void);

/*
 * basil_reserve - create a BASIL reservation.
 * IN job_ptr - pointer to job which has just been allocated resources
 * RET 0 or error code
 */
extern int basil_reserve(struct job_record *job_ptr);

/*
 * basil_release - release a BASIL reservation by job.
 * IN job_ptr - pointer to job which has just been deallocated resources
 * RET 0 or error code
 */
extern int basil_release(struct job_record *job_ptr);

/*
 * basil_release_id - release a BASIL reservation by ID.
 * IN reservation_id - ID of reservation to release
 * RET 0 or error code
 */
extern int basil_release_id(char *reservation_id);

#endif	/* !_HAVE_BASIL_INTERFACE_H */
