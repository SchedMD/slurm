/*****************************************************************************\
 * src/slurmd/slurmd/req.h - slurmd request handling
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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
#ifndef _SLURMD_REQ_H
#define _SLURMD_REQ_H

#include "src/common/slurm_protocol_defs.h"

/* Process request contained in slurm message `msg' from client at
 * msg->orig_addr
 *
 * If msg == NULL, then purge allocated memory.
 */
void slurmd_req(slurm_msg_t *msg);

void destroy_starting_step(void *x);

void gids_cache_purge(void);

/* Add record for every launched job so we know they are ready for suspend */
extern void record_launched_jobs(void);

void file_bcast_init(void);
void file_bcast_purge(void);

/*
 * ume_notify - Notify all jobs and steps on this node that a Uncorrectable
 *	Memory Error (UME) has occured by sending SIG_UME (to log event in
 *	stderr)
 * RET count of signaled job steps
 */
extern int ume_notify(void);

#endif
