/*****************************************************************************\
 *  slurm_energy_accounting.h - implementation-independent job completion 
 *  logging API definitions 
 *****************************************************************************
 *  Written by Bull-HN-PHX/d.rusak,
 *  Copyright (C) 2012 Bull-HN-PHX
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

/*****************************************************************************\
 *  Modification history
 *
 *  19 Jan 2005 by Andy Riebs <andy.riebs@hp.com>
 *       This file is derived from the file slurm_JOBACCT.c, written by
 *       Morris Jette, et al.
\*****************************************************************************/

#ifndef __SLURM_ENERGY_ACCOUNTING_H__
#define __SLURM_ENERGY_ACCOUNTING_H__

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

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/slurm_jobacct_gather.h"

extern int slurm_energy_accounting_init(void); /* load the plugin */
extern int slurm_energy_accounting_fini(void); /* unload the plugin */

extern uint32_t energy_accounting_g_getcurrentwatts(void);
extern uint32_t energy_accounting_g_getbasewatts(void);
extern uint32_t energy_accounting_g_getnodeenergy(uint32_t up_time);
extern int energy_accounting_g_updatenodeenergy(void);

extern jobacctinfo_t *energy_accounting_g_create(jobacct_id_t *jobacct_id);
extern void energy_accounting_g_destroy(jobacctinfo_t *jobacct);
extern int energy_accounting_g_getenergy(void);

extern void energy_accounting_g_aggregate(jobacctinfo_t *dest,
				       jobacctinfo_t *from);

extern void energy_accounting_g_change_poll(uint16_t frequency);
extern int  energy_accounting_g_startpoll(uint16_t frequency);
extern int  energy_accounting_g_endpoll(void);
extern void energy_accounting_g_suspend_poll(void);
extern void energy_accounting_g_resume_poll(void);

#endif /*__SLURM_ENERGY_ACCOUNTING_H__*/
