/*****************************************************************************\
 *  basil_resv_conf.h - user interface to BASIL for confirming a resource
 *	reservation. BASIL is Cray's Batch Application Scheduler Interface
 *	Layer.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif		/* HAVE_INTTYPES_H */
#endif

#include <slurm/slurm_errno.h>

#include "src/common/log.h"

#define BASIL_DEBUG 1

/*
 * basil_resv_conf - confirm a previously created BASIL resource reservation.
 *	This must be called from the same container from which the user
 *	application is to run. The container is normally a Linux Process
 *	Group or SGI Process Aggregate (see http://oss.sgi.com/projects/pagg).
 * IN reservation_id - ID of reservation conform
 * IN job_id - SLURM job ID
 * RET 0 or error code
 */
extern int basil_resv_conf(char *reservation_id, uint32_t job_id)
{
	int error_code = SLURM_SUCCESS;
#ifdef HAVE_CRAY_XT
#ifdef APBASIL_LOC
	/* Issue the BASIL CONFIRM request */
	if (request_failure) {
		error("basil confirm of %s error: %s", reservation_id, "TBD");
		return SLURM_ERROR;
	}
	debug("basil confirm of reservation %s by job %u complete",
	      reservation_id, job_id);
#else
	debug("basil confirm of reservation %s by job %u complete",
	      reservation_id, job_id);
#endif	/* APBASIL_LOC */
#endif	/* HAVE_CRAY_XT */
	return error_code;
}
