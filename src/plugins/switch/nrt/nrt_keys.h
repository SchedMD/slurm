/*****************************************************************************\
 **  nrt_keys.h - Key definitions used by the get_jobinfo functions
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jason King <jking@llnl.gov>
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

#ifndef _NRT_KEYS_INCLUDED
#define _NRT_KEYS_INCLUDED

#if HAVE_NRT_H
# include <nrt.h>
#else
# error "Must have nrt.h to compile this module!"
#endif

enum {
	/* NRT specific get_jobinfo keys */
	NRT_JOBINFO_TABLEINFO,
	NRT_JOBINFO_TABLESPERTASK,	/* Count of nrt_tableinfo records */
	NRT_JOBINFO_KEY,
	NRT_JOBINFO_PROTOCOL,
	NRT_JOBINFO_MODE
};

/* Information shared between slurm_ll_api and the slurm NRT driver */
typedef struct nrt_tableinfo {
	char adapter_name[NRT_MAX_ADAPTER_NAME_LEN]; /* eth0, mlx4_0, etc. */
	nrt_adapter_t adapter_type;
	nrt_context_id_t context_id;
	uint32_t instance;
	nrt_network_id_t network_id;
	char protocol_name[NRT_MAX_PROTO_NAME_LEN];  /* MPI, LAPI, UPC, etc. */
	nrt_table_id_t table_id;
	uint32_t table_length;
	void *table; /* Pointer to nrt_*_task_info_t */
} nrt_tableinfo_t;

/* In order to determine the adapters and protocols in use:
 * int table_cnt;
 * nrt_tableinfo_t *tables;
 * switch_p_get_jobinfo(switch_job_ptr, NRT_JOBINFO_TABLESPERTASK, &table_cnt);
 * switch_p_get_jobinfo(switch_job_ptr, NRT_JOBINFO_TABLESPERTASK, &table);
 * for (i=0, table_ptr=table; i<table_cnt; i++, table_ptr++) {
 *   printf("adapter:%s protocol:%s\n", table_ptr->adapter_name,
 *          table_ptr->protocol_name);
 * }
 */

#endif /* _NRT_KEYS_INCLUDED */
