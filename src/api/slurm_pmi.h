/****************************************************************************\
 *  slurm_pmi.h - definitions PMI support functions internal to SLURM
 *****************************************************************************
 *  Copyright (C) 2005-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#ifndef _SLURM_PMI_H
#define _SLURM_PMI_H

#include <inttypes.h>

#include "src/common/pack.h"
#include "src/common/slurm_protocol_defs.h"

#define PMI_MAX_ID_LEN       16	/* Maximim size of PMI process group ID */
#define PMI_MAX_KEY_LEN     256	/* Maximum size of a PMI key */
#define PMI_MAX_KVSNAME_LEN 256	/* Maximum size of KVS name */
#define PMI_MAX_VAL_LEN     1024 /* Maximum size of a PMI value */

/*
 * The following functions MUST NOT change signature. They define the ABI for
 * libslurm_pmi which is intentionally unversioned, and only use for PMI1
 * support.
 *
 * These are for use through libslurm_pmi.so, which is an unversioned copy of
 * libslurm designed to work around static linking issues with OpenMPI.
 *
 * When OpenMPI statically links against libpmi.so (as provided by Slurm),
 * it inherits a dependency on libslurm_pmi.so, and the slurm_pmi_* symbols
 * needed for libpmi to run. By making this a separate unversioned library,
 * when Slurm is upgraded between releases the existing OpenMPI installs will
 * no longer need to be recompiled.
 *
 * For 20.02 and older, libpmi.so links to libslurm.so.<version> which is
 * only installed for each given release, and will be removed by RPM or
 * other package managers. Thus breaking any OpenMPI version that statically
 * linked against our libpmi.so as they inherited the libslurm.so.<version>
 * dependency. By providing an unversioned libslurm_pmi.so, we avoid that
 * issue in 20.11 and up. As long as the slurm_pmi_* ABI remains unchanged
 * this should work without issue.
 */

/* Transmit PMI Keyval space data */
extern int slurm_pmi_send_kvs_comm_set(kvs_comm_set_t *kvs_set_ptr,
				       int pmi_rank, int pmi_size);

/* Wait for barrier and get full PMI Keyval space data */
extern int slurm_pmi_get_kvs_comm_set(kvs_comm_set_t **kvs_set_ptr,
				      int pmi_rank, int pmi_size);

extern void slurm_pmi_free_kvs_comm_set(kvs_comm_set_t *msg);

/* Finalization processing */
extern void slurm_pmi_finalize(void);

/* Wrapper for slurm_kill_job_step(). */
extern int slurm_pmi_kill_job_step(uint32_t job_id, uint32_t step_id,
				   uint16_t signal);

#endif
