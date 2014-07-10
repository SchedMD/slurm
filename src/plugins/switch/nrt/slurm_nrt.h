/*****************************************************************************\
 *  slurm_nrt.h - Library routines for initiating jobs using IBM's NRT
 *                (Network Routing Table)
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Portions Copyright (C) 2011 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jason King <jking@llnl.gov>
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/common/slurm_xlator.h"

#ifndef _SLURM_NRT_INCLUDED
#define _SLURM_NRT_INCLUDED

#if HAVE_NRT_H
# include <nrt.h>
#else
# error "Must have nrt.h to compile this module!"
#endif

/* opaque data structures - no peeking! */
typedef struct slurm_nrt_libstate slurm_nrt_libstate_t;
typedef struct slurm_nrt_jobinfo  slurm_nrt_jobinfo_t;
typedef struct slurm_nrt_nodeinfo slurm_nrt_nodeinfo_t;

/* NOTE: error codes should be between ESLURM_SWITCH_MIN and
 * ESLURM_SWITCH MAX as defined in slurm/slurm_errno.h */
enum {
	/* switch/nrt specific error codes */
	ESTATUS =					3000,
	EADAPTER,
	ENOADAPTER,
	EBADMAGIC_NRT_NODEINFO,
	EBADMAGIC_NRT_JOBINFO,
	EBADMAGIC_NRT_LIBSTATE,
	EUNPACK,
	EHOSTNAME,
	ENOTSUPPORTED,
	EVERSION,
	EWINDOW,
	EUNLOAD
};

#define NRT_DEBUG_CNT 0	/* Count of windows, adapters, etc to log
			 * use this to limit volume of logging */
#define NRT_MAXADAPTERS 9
#define NRT_LIBSTATE_LEN (1024 * 1024 * 1)

extern uint64_t debug_flags;

extern bool nrt_adapter_name_check(char *token, hostlist_t hl);
extern int nrt_clear_node_state(void);
extern char *nrt_err_str(int rc);
extern int nrt_slurmctld_init(void);
extern int nrt_slurmd_init(void);
extern int nrt_slurmd_step_init(void);
extern int nrt_alloc_nodeinfo(slurm_nrt_nodeinfo_t **nh);
extern int nrt_build_nodeinfo(slurm_nrt_nodeinfo_t *np, char *hostname);
/* extern char *nrt_print_nodeinfo(slurm_nrt_nodeinfo_t *np, char *buf,
				size_t size);	* Incomplete */
extern int nrt_pack_nodeinfo(slurm_nrt_nodeinfo_t *np, Buf buf,
			     uint16_t protocol_version);
extern int nrt_unpack_nodeinfo(slurm_nrt_nodeinfo_t *np, Buf buf,
			       uint16_t protocol_version);
extern void nrt_free_nodeinfo(slurm_nrt_nodeinfo_t *np, bool ptr_into_array);
extern int nrt_alloc_jobinfo(slurm_nrt_jobinfo_t **jh);
extern int nrt_build_jobinfo(slurm_nrt_jobinfo_t *jp, hostlist_t hl,
			     uint16_t *tasks_per_node, uint32_t **tids,
			     bool sn_all,
			     char *adapter_name, nrt_adapter_t dev_type,
			     bool bulk_xfer, uint32_t bulk_xfer_resources,
			     bool ip_v4, bool user_space, char *protocol,
			     int instances, int cau, int immed);
extern int nrt_pack_jobinfo(slurm_nrt_jobinfo_t *jp, Buf buf,
			    uint16_t protocol_version);
extern int nrt_unpack_jobinfo(slurm_nrt_jobinfo_t *jp, Buf buf,
			      uint16_t protocol_version);
extern void nrt_free_jobinfo(slurm_nrt_jobinfo_t *jp);
extern int nrt_load_table(slurm_nrt_jobinfo_t *jp, int uid, int pid,
			  char *job_name);
extern int nrt_init(void);
extern int nrt_fini(void);
extern int nrt_unload_table(slurm_nrt_jobinfo_t *jp);
extern int nrt_get_jobinfo(slurm_nrt_jobinfo_t *jp, int key, void *data);
extern void nrt_libstate_save(Buf buffer, bool free_flag);
extern int nrt_libstate_restore(Buf buffer);
extern int nrt_job_step_complete(slurm_nrt_jobinfo_t *jp, hostlist_t hl);
extern int nrt_job_step_allocated(slurm_nrt_jobinfo_t *jp, hostlist_t hl);
extern int nrt_libstate_clear(void);
extern int nrt_slurmctld_init(void);
extern int nrt_slurmd_init(void);
extern int nrt_slurmd_step_init(void);
extern int nrt_preempt_job_test(slurm_nrt_jobinfo_t *jp);
extern int nrt_preempt_job(void *suspend_info, int max_wait_secs);
extern int nrt_resume_job(void *suspend_info, int max_wait_secs);
extern void nrt_suspend_job_info_get(slurm_nrt_jobinfo_t *jp,
				     void **suspend_info);
extern void nrt_suspend_job_info_pack(void *suspend_info, Buf buffer,
				      uint16_t protocol_version);
extern int nrt_suspend_job_info_unpack(void **suspend_info, Buf buffer,
				       uint16_t protocol_version);
extern void nrt_suspend_job_info_free(void *suspend_info);

#endif /* _SLURM_NRT_INCLUDED */
