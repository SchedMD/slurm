/****************************************************************************\
 *  slurm_pmi.h - definitions PMI support functions internal to SLURM
 *****************************************************************************
 *  Copyright (C) 2005-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#ifndef _SLURM_PMI_H
#define _SLURM_PMI_H

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

#include "src/common/pack.h"

#define PMI_MAX_ID_LEN       16	/* Maximim size of PMI process group ID */
#define PMI_MAX_KEY_LEN     256	/* Maximum size of a PMI key */
#define PMI_MAX_KVSNAME_LEN 256	/* Maximum size of KVS name */
#define PMI_MAX_VAL_LEN     256	/* Maximum size of a PMI value */

struct kvs_hosts {
	uint16_t	task_id;	/* job step's task id */
	uint16_t	port;		/* communication port */
	char *		hostname;	/* communication host */
};
struct kvs_comm {
	char *		kvs_name;
	uint16_t	kvs_cnt;	/* count of key-pairs */
	char **		kvs_keys;
	char **		kvs_values;
};
struct kvs_comm_set {

	uint16_t	host_cnt;	/* hosts getting this message */
	struct kvs_hosts *kvs_host_ptr;	/* host forwarding info */
 	uint16_t	kvs_comm_recs;	/* count of kvs_comm entries */
	struct kvs_comm **kvs_comm_ptr;	/* pointers to kvs_comm entries */
};

/* Transmit PMI Keyval space data */
int slurm_send_kvs_comm_set(struct kvs_comm_set *kvs_set_ptr,
		int pmi_rank, int pmi_size);

/* Wait for barrier and get full PMI Keyval space data */
int  slurm_get_kvs_comm_set(struct kvs_comm_set **kvs_set_ptr, 
		int pmi_rank, int pmi_size);

/* Free kvs_comm_set returned by slurm_get_kvs_comm_set() */
void slurm_free_kvs_comm_set(struct kvs_comm_set *kvs_set_ptr);

/* Finalization processing */
void slurm_pmi_finalize(void);

#endif
