/*****************************************************************************\
 **  nrt_keys.h - Key definitions used by the get_jobinfo functions
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jason King <jking@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _NRT_KEYS_INCLUDED
#define _NRT_KEYS_INCLUDED

#if HAVE_LIBNRT
# include <nrt.h>
#else
# error "Must have libnrt to compile this module!"
#endif

enum {
	/* NRT specific get_jobinfo keys */
	NRT_JOBINFO_TABLEINFO,
	NRT_JOBINFO_TABLESPERTASK,
	NRT_JOBINFO_KEY,
	NRT_JOBINFO_PROTOCOL,
	NRT_JOBINFO_MODE,
	NRT_JOBINFO_COMM_INFO
};

/* Information shared between slurm_ll_api and the slurm NRT driver */
typedef struct nrt_comm_record {
	nrt_context_id_t context_id;
	nrt_table_id_t   table_id;
	char device_name[NRT_MAX_DEVICENAME_SIZE];   /* eth0, mlx4_0, etc. */
	char protocol_name[NRT_MAX_PROTO_NAME_LEN];  /* MPI, LAPI, UPC, etc. */
} nrt_comm_record_t;

typedef struct nrt_comm_table {
	uint16_t nrt_comm_count;
	nrt_comm_record_t *nrt_comm_ptr;
} nrt_comm_table_t;

typedef struct nrt_tableinfo {
	uint32_t table_length;
	void *table; /* Pointer to nrt_*_task_info_t */
	char adapter_name[NRT_MAX_ADAPTER_NAME_LEN];
	nrt_adapter_t adapter_type;
	nrt_network_id_t network_id;
/* FIXME: Need to populate, un/pack, and free this data structure */
	nrt_comm_table_t *comm_table_ptr;
} nrt_tableinfo_t;

#endif /* _NRT_KEYS_INCLUDED */
