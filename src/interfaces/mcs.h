/*****************************************************************************\
 *  slurm_mcs.h - Define mcs plugin functions
 *****************************************************************************
 *  Copyright (C) 2015 CEA/DAM/DIF
 *  Written by Aline Roy <aline.roy@cea.fr>
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

#ifndef _SLURM_MCS_H
#define _SLURM_MCS_H

#include <inttypes.h>

#include "src/slurmctld/slurmctld.h"

#define MCS_SELECT_NOSELECT		0x00
#define MCS_SELECT_ONDEMANDSELECT	0x01
#define MCS_SELECT_SELECT		0x02

extern int slurm_mcs_init(void);
extern int slurm_mcs_fini(void);
extern int slurm_mcs_reconfig(void);
extern char *slurm_mcs_get_params_specific(void);
extern int slurm_mcs_reset_params(void);
extern int slurm_mcs_get_select(job_record_t *job_ptr);
extern int slurm_mcs_get_enforced(void);
extern int slurm_mcs_get_privatedata(void);
extern char *slurm_mcs_get_params_specific(void);
extern int mcs_g_set_mcs_label(job_record_t *job_ptr, char *label);
extern int mcs_g_check_mcs_label(uint32_t user_id, char *mcs_label,
				 bool assoc_locked);

#endif /*_SLURM_MCS_H */
