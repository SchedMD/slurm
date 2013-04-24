/*****************************************************************************\
 * src/slurmd/slurmstepd/slurmstepd.h - slurmstepd general header file
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>.
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

#ifndef _SLURMSTEPD_H
#define _SLURMSTEPD_H

#include "src/common/bitstring.h"

#define STEPD_MESSAGE_COMP_WAIT 3 /* seconds */
#define MAX_RETRIES    3

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

#endif /* !_SLURMSTEPD_H */
