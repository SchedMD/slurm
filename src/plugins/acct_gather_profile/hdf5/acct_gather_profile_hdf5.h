/*****************************************************************************\
 *  io_energy.h - slurm energy accounting plugin for io and energy using hdf5.
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#ifndef _GATHER_PROFILE_IO_ENERGY_H_
#define _GATHER_PROFILE_IO_ENERGY_H_

#include "src/common/slurm_acct_gather_profile.h"

#define PROFILE_DEFAULT_PROFILE "none"

// See /common/slurm_acct_gather.h for details on function signatures
extern int acct_gather_profile_p_controller_start();
extern int acct_gather_profile_p_node_step_start(slurmd_job_t* job);
extern int acct_gather_profile_p_node_step_end(slurmd_job_t* job);
extern int acct_gather_profile_p_task_start(slurmd_job_t* job,uint32_t taskno);
extern int acct_gather_profile_p_task_end(slurmd_job_t* job, pid_t taskpid);
extern int acct_gather_profile_p_job_sample();
extern int acct_gather_profile_p_add_node_data(slurmd_job_t* job, char* group,
		char* type, void* data);
extern int acct_gather_profile_p_add_sample_data(char* group, char* type,
		void* data);
extern int acct_gather_profile_p_add_task_data(slurmd_job_t* job,
		uint32_t taskid, char* group, char* type, void* data);

extern int init ( void );
extern int fini ( void );
extern void acct_gather_profile_p_conf_options(s_p_options_t **full_options,
					      int *full_options_cnt);
extern void acct_gather_profile_p_conf_set(s_p_hashtbl_t *tbl);
extern void* acct_gather_profile_p_conf_get();

#endif
