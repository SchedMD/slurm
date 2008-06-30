/*****************************************************************************\
 **  federation.h - Library routines for initiating jobs on IBM Federation
 **  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jason King <jking@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#endif

#include "src/common/slurm_xlator.h"

#ifndef _FEDERATION_INCLUDED
#define _FEDERATION_INCLUDED

#if HAVE_LIBNTBL
# include <ntbl.h>
#else
# error "Don't have required libntbl!"
#endif

/* opaque data structures - no peeking! */
typedef struct fed_libstate fed_libstate_t;
typedef struct fed_jobinfo fed_jobinfo_t;
typedef struct fed_nodeinfo fed_nodeinfo_t;

/* NOTE: error codes should be between ESLURM_SWITCH_MIN and 
 * ESLURM_SWITCH MAX as defined in slurm/slurm_errno.h */
enum {
	/* Federation specific error codes */
	ESTATUS =					3000,
	EADAPTER,
	ENOADAPTER,
	EBADMAGIC_FEDNODEINFO,
	EBADMAGIC_FEDJOBINFO,
	EBADMAGIC_FEDLIBSTATE,
	EUNPACK,
	EHOSTNAME,
	ENOTSUPPORTED,
	EVERSION,
	EWINDOW,
	EUNLOAD
};

#define FED_MAXADAPTERS 2
#define FED_LIBSTATE_LEN (1024 * 1024 * 1)

int fed_slurmctld_init(void);
int fed_slurmd_init(void);
int fed_slurmd_step_init(void);
int fed_alloc_nodeinfo(fed_nodeinfo_t **nh);
int fed_build_nodeinfo(fed_nodeinfo_t *np, char *hostname);
char *fed_print_nodeinfo(fed_nodeinfo_t *np, char *buf, size_t size);
int fed_pack_nodeinfo(fed_nodeinfo_t *np, Buf buf);
int fed_unpack_nodeinfo(fed_nodeinfo_t *np, Buf buf);
void fed_free_nodeinfo(fed_nodeinfo_t *np, bool ptr_into_array);
int fed_alloc_jobinfo(fed_jobinfo_t **jh);
int fed_build_jobinfo(fed_jobinfo_t *jp, hostlist_t hl, int nprocs,
		      bool sn_all, char *adapter_name, int bulk_xfer);
int fed_pack_jobinfo(fed_jobinfo_t *jp, Buf buf);
int fed_unpack_jobinfo(fed_jobinfo_t *jp, Buf buf);
fed_jobinfo_t *fed_copy_jobinfo(fed_jobinfo_t *jp);
void fed_free_jobinfo(fed_jobinfo_t *jp);
int fed_load_table(fed_jobinfo_t *jp, int uid, int pid);
int fed_init(void);
int fed_fini(void);
int fed_unload_table(fed_jobinfo_t *jp);
int fed_unpack_libstate(fed_libstate_t *lp, Buf buffer);
int fed_get_jobinfo(fed_jobinfo_t *jp, int key, void *data);
void fed_libstate_save(Buf buffer, bool free_flag);
int fed_libstate_restore(Buf buffer);
int fed_job_step_complete(fed_jobinfo_t *jp, hostlist_t hl);
int fed_job_step_allocated(fed_jobinfo_t *jp, hostlist_t hl);
int fed_libstate_clear(void);
int fed_slurmctld_init(void);
int fed_slurmd_init(void);
int fed_slurmd_step_init(void);

#endif /* _FEDERATION_INCLUDED */
