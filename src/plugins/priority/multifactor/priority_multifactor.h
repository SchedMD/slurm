/*****************************************************************************\
 *  priority_multifactor.c - slurm multifactor priority plugin.
 *****************************************************************************
 *
 *  Copyright (C) 2012  Aalto University
 *  Written by Janne Blomqvist <janne.blomqvist@aalto.fi>
 *
 *  Based on priority_multifactor.c, whose copyright information is
 *  reproduced below:
 *
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#ifndef _PRIORITY_MULTIFACTOR_H
#define _PRIORITY_MULTIFACTOR_H

#include "src/common/slurm_priority.h"
#include "src/common/assoc_mgr.h"

#include "src/slurmctld/locks.h"
extern void priority_p_set_assoc_usage(slurmdb_association_rec_t *assoc);
extern double priority_p_calc_fs_factor(
		long double usage_efctv, long double shares_norm);
extern bool decay_apply_new_usage(
		struct job_record *job_ptr, time_t *start_time_ptr);
extern int  decay_apply_weighted_factors(
		struct job_record *job_ptr, time_t *start_time_ptr);
extern void set_assoc_usage_norm(slurmdb_association_rec_t *assoc);
extern void set_priority_factors(time_t start_time, struct job_record *job_ptr);

extern bool priority_debug;

#endif
