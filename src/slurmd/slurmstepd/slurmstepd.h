/*****************************************************************************\
 * src/slurmd/slurmstepd/slurmstepd.h - slurmstepd general header file
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>.
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

#ifndef _SLURMSTEPD_H
#define _SLURMSTEPD_H

#include "src/common/bitstring.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#define STEPD_MESSAGE_COMP_WAIT 3 /* seconds */

extern int slurmstepd_blocked_signals[];

typedef struct {
	pthread_cond_t cond;
	pthread_mutex_t lock;
	int rank;
	int depth;
	int parent_rank;
	slurm_addr_t parent_addr;
	int children;
	int max_depth;
	bool wait_children;
	bitstr_t *bits;
	int step_rc;
	jobacctinfo_t *jobacct;
} step_complete_t;

extern step_complete_t step_complete;

extern slurmd_conf_t *conf;

extern int stepd_cleanup(slurm_msg_t *msg, stepd_step_rec_t *job,
			 slurm_addr_t *cli, slurm_addr_t *self,
			 int rc, bool only_mem);
extern int stepd_drain_node(char *reason);
extern int stepd_send_pending_exit_msgs(stepd_step_rec_t *job);
extern void stepd_send_step_complete_msgs(stepd_step_rec_t *job);
extern void stepd_wait_for_children_slurmstepd(stepd_step_rec_t *job);

extern void close_slurmd_conn(void);

#endif /* !_SLURMSTEPD_H */
