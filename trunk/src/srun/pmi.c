/*****************************************************************************\
 *  pmi.c - Global PMI data as maintained within srun
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <pthread.h>
#include <slurm/slurm_errno.h>

#include "src/api/slurm_pmi.h"
#include "src/common/xmalloc.h"

#define _DEBUG 1

/* Global variables */
pthread_mutex_t kvs_mutex = PTHREAD_MUTEX_INITIALIZER;
int kvs_comm_cnt = 0;
struct kvs_comm **kvs_comm_ptr = NULL;

/* return pointer to named kvs element or NULL if not found */
static struct kvs_comm *_find_kvs_by_name(char *name)
{
	int i;

	for (i=0; i<kvs_comm_cnt; i++) {
		if (strcmp(kvs_comm_ptr[i]->kvs_name, name))
			continue;
		return kvs_comm_ptr[i];
	}
	return NULL;
}

static void _merge_named_kvs(struct kvs_comm *kvs_orig, 
		struct kvs_comm *kvs_new)
{
	int i, j;
	for (i=0; i<kvs_new->kvs_cnt; i++) {
		for (j=0; j<kvs_orig->kvs_cnt; j++) {
			if (strcmp(kvs_new->kvs_keys[i], kvs_orig->kvs_keys[j]))
				continue;
			xfree(kvs_orig->kvs_values[j]);
			kvs_orig->kvs_values[j] = kvs_new->kvs_values[i];
			kvs_new->kvs_values[i] = NULL;
			break;
		}
		if (j < kvs_orig->kvs_cnt)
			continue;	/* already recorded, update */
		/* append it */
		kvs_orig->kvs_cnt++;
		xrealloc(kvs_orig->kvs_keys, 
				(sizeof(char *) * kvs_orig->kvs_cnt));
		xrealloc(kvs_orig->kvs_values, 
				(sizeof(char *) * kvs_orig->kvs_cnt));
		kvs_orig->kvs_keys[kvs_orig->kvs_cnt-1] = kvs_new->kvs_keys[i];
		kvs_orig->kvs_values[kvs_orig->kvs_cnt-1] = 
				kvs_new->kvs_values[i];
		kvs_new->kvs_keys[i] = NULL;
		kvs_new->kvs_values[i] = NULL;
	}
}

static void _move_kvs(struct kvs_comm *kvs_new)
{
	kvs_comm_ptr = xrealloc(kvs_comm_ptr, (sizeof(struct kvs_comm *) *
			(kvs_comm_cnt + 1)));
	kvs_comm_ptr[kvs_comm_cnt] = kvs_new;
	kvs_comm_cnt++;
}

static void _print_kvs(void)
{
#if _DEBUG
	int i, j;

	for (i=0; i<kvs_comm_cnt; i++) {
		for (j=0; j<kvs_comm_ptr[i]->kvs_cnt; j++) {
			info("KVS: %s:%s:%s", kvs_comm_ptr[i]->kvs_name,
				kvs_comm_ptr[i]->kvs_keys[j],
				kvs_comm_ptr[i]->kvs_values[j]); 
		}
	}
#endif
}

extern int pmi_kvs_put(struct kvs_comm_set *kvs_set_ptr)
{
	int i;
	struct kvs_comm *kvs_ptr;
	/* Merge new data with old.
	 * NOTE: We just move pointers rather than copy data where 
	 * possible for improved performance */
	pthread_mutex_lock(&kvs_mutex);
	for (i=0; i<kvs_set_ptr->kvs_comm_recs; i++) {
		kvs_ptr = _find_kvs_by_name(kvs_set_ptr->
			kvs_comm_ptr[i]->kvs_name);
		if (kvs_ptr) {
			_merge_named_kvs(kvs_ptr, 
				kvs_set_ptr->kvs_comm_ptr[i]);
		} else {
			_move_kvs(kvs_set_ptr-> kvs_comm_ptr[i]);
			kvs_set_ptr-> kvs_comm_ptr[i] = NULL;
		}
	}
	slurm_free_kvs_comm_set(kvs_set_ptr);
	_print_kvs();
	pthread_mutex_unlock(&kvs_mutex);
	return SLURM_SUCCESS;
}
