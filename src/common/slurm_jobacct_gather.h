/*****************************************************************************\
 *  slurm_jobacct_gather.h - implementation-independent job completion logging
 *  API definitions
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.com> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
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

/*****************************************************************************\
 *  Modification history
 *
 *  19 Jan 2005 by Andy Riebs <andy.riebs@hp.com>
 *       This file is derived from the file slurm_JOBACCT.c, written by
 *       Morris Jette, et al.
\*****************************************************************************/


#ifndef __SLURM_JOBACCT_GATHER_H__
#define __SLURM_JOBACCT_GATHER_H__

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include <sys/resource.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <slurm/slurm.h>

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/jobacct_common.h"

extern int slurm_jobacct_gather_init(void); /* load the plugin */
extern int slurm_jobacct_gather_fini(void); /* unload the plugin */

extern jobacctinfo_t *jobacct_gather_g_create(jobacct_id_t *jobacct_id);
extern void jobacct_gather_g_destroy(jobacctinfo_t *jobacct);
extern int jobacct_gather_g_setinfo(jobacctinfo_t *jobacct,
				    enum jobacct_data_type type, void *data);
extern int jobacct_gather_g_getinfo(jobacctinfo_t *jobacct,
				    enum jobacct_data_type type, void *data);
extern void jobacct_gather_g_pack(jobacctinfo_t *jobacct, Buf buffer);
extern int jobacct_gather_g_unpack(jobacctinfo_t **jobacct, Buf buffer);

extern void jobacct_gather_g_aggregate(jobacctinfo_t *dest,
				       jobacctinfo_t *from);

extern void jobacct_gather_g_change_poll(uint16_t frequency);
extern int  jobacct_gather_g_startpoll(uint16_t frequency);
extern int  jobacct_gather_g_endpoll();
extern void jobacct_gather_g_suspend_poll();
extern void jobacct_gather_g_resume_poll();

extern int jobacct_gather_g_set_proctrack_container_id(uint32_t id);
extern int jobacct_gather_g_add_task(pid_t pid, jobacct_id_t *jobacct_id);
/* must free jobacctinfo_t if not NULL */
extern jobacctinfo_t *jobacct_gather_g_stat_task(pid_t pid);
/* must free jobacctinfo_t if not NULL */
extern jobacctinfo_t *jobacct_gather_g_remove_task(pid_t pid);

extern void jobacct_gather_g_2_sacct(sacct_t *sacct, jobacctinfo_t *jobacct);


#endif /*__SLURM_JOBACCT_GATHER_H__*/

