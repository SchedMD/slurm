/*****************************************************************************\
 *  src/common/power.h - Generic power management plugin wrapper functions.
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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

#ifndef _SLURM_POWER_H
#define _SLURM_POWER_H

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/slurmctld/slurmctld.h"

/*****************************************************************************\
 * PLUGIN FUNCTIONS
\*****************************************************************************/
/* Initialize the power plugin */
extern int power_g_init(void);

/* Terminate the power plugin and free all memory */
extern void power_g_fini(void);

/* Read the configuration file */
extern void power_g_reconfig(void);

/* Note that a suspended job has been resumed */
extern void power_g_job_resume(struct job_record *job_ptr);

/* Note that a job has been allocated resources and is ready to start */
extern void power_g_job_start(struct job_record *job_ptr);

/*****************************************************************************\
 * GENERIC DATA MOVEMENT FUNCTIONS
\*****************************************************************************/
/* Pack a power management data structure */
extern void power_mgmt_data_pack(power_mgmt_data_t *power, Buf buffer,
				 uint16_t protocol_version);

/* Unpack a power management data structure
 * Use power_mgmt_data_free() to free the returned structure */
extern int power_mgmt_data_unpack(power_mgmt_data_t **power, Buf buffer,
				  uint16_t protocol_version);

/* Free a power management data structure */
extern void power_mgmt_data_free(power_mgmt_data_t *power);

#endif /* _SLURM_POWER_H */
