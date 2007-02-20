/****************************************************************************\
 *  slurm_pmi.h - definitions PMI support functions internal to SLURM
 *****************************************************************************
 *  Copyright (C) 2005-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
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

struct kvs_comm {
	char *		kvs_name;
	uint16_t	kvs_cnt;	/* count of key-pairs */
	char **		kvs_keys;
	char **		kvs_values;
};
struct kvs_comm_set {
	uint16_t	task_id;	/* job step's task id */
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

#endif
