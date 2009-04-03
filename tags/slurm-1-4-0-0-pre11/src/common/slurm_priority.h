/*****************************************************************************\
 *  slurm_priority.h - Define priority plugin functions
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#ifndef _SLURM_PRIORITY_H 
#define _SLURM_PRIORITY_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif
#if HAVE_STDINT_H
#  include <stdint.h>           /* for uint16_t, uint32_t definitions */
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>         /* for uint16_t, uint32_t definitions */
#endif

#include "src/slurmctld/slurmctld.h"
#include "src/common/slurm_accounting_storage.h"

extern int slurm_priority_init(void);
extern int slurm_priority_fini(void);
extern uint32_t priority_g_set(uint32_t last_prio, struct job_record *job_ptr);
extern void priority_g_reconfig();
/*
 * set up how much usage can happen on the cluster during a given half
 * life.  This can only be done after we get a correct proc count for
 * the system.
 * IN: procs - number of proccessors on the system
 * IN: half_life - time half_life is in seconds.
 * RET: SLURM_SUCCESS on SUCCESS, SLURM_ERROR else.
 */
extern int priority_g_set_max_cluster_usage(uint32_t procs, uint32_t half_life);

/* sets up the normalized usage and the effective usage of an
 * association.
 * IN/OUT: assoc - association to have usage set.
 */
extern void priority_g_set_assoc_usage(acct_association_rec_t *assoc);
extern List priority_g_get_priority_factors_list(
	priority_factors_request_msg_t *req_msg);

#endif /*_SLURM_PRIORIY_H */
