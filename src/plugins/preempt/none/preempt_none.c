/*****************************************************************************\
 *  preempt_none.c - disable job preemption plugin.
 *****************************************************************************
 *  Copyright (C) 2009-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <http://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris jette <jette1@llnl.gov>
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

#include <stdio.h>

#include "slurm/slurm_errno.h"

#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/plugin.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/job_scheduler.h"

const char	plugin_name[]	= "Preemption disabled";
const char	plugin_type[]	= "preempt/none";
const uint32_t	plugin_version	= 100;

/**************************************************************************/
/*  TAG(                              init                              ) */
/**************************************************************************/
extern int init( void )
{
	verbose("preempt/none loaded");
	return SLURM_SUCCESS;
}

/**************************************************************************/
/*  TAG(                              fini                              ) */
/**************************************************************************/
extern void fini( void )
{
	/* Empty. */
}

/**************************************************************************/
/* TAG(                 find_preemptable_jobs                           ) */
/**************************************************************************/
extern List find_preemptable_jobs(struct job_record *job_ptr)
{
	return (List) NULL;
}

/**************************************************************************/
/* TAG(                 job_preempt_mode                                ) */
/**************************************************************************/
extern uint16_t job_preempt_mode(struct job_record *job_ptr)
{
	return (uint16_t) PREEMPT_MODE_OFF;
}

/**************************************************************************/
/* TAG(                 preemption_enabled                              ) */
/**************************************************************************/
extern bool preemption_enabled(void)
{
	return false;
}

/***************************************************************************/
/* Return true if the preemptor can preempt the preemptee, otherwise false */
/***************************************************************************/
extern bool job_preempt_check(job_queue_rec_t *preemptor,
			      job_queue_rec_t *preemptee)
{
	return false;
}
