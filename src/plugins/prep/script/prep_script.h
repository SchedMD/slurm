/*****************************************************************************\
 *  prep_script.h - PrEp script plugin, handles Prolog / Epilog /
 *		    PrologSlurmctld / EpilogSlurmctld scripts
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#ifndef _PREP_SCRIPT_H_
#define _PREP_SCRIPT_H_

#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd/slurmd.h"

extern void (*prolog_slurmctld_callback)(int rc, uint32_t job_id);
extern void (*epilog_slurmctld_callback)(int rc, uint32_t job_id);

extern void slurmctld_script(job_record_t *job_ptr, bool is_epilog);
extern int slurmd_script(job_env_t *job_env, slurm_cred_t *cred,
			 bool is_epilog);

#endif
