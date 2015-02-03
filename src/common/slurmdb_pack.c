/*****************************************************************************\
 *  slurmdb_pack.h - un/pack definitions used by slurmdb api
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
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

#include <stdlib.h>
#include "slurmdb_pack.h"
#include "slurmdbd_defs.h"
#include "slurm_protocol_defs.h"
#include "list.h"
#include "pack.h"

static void _pack_slurmdb_stats(slurmdb_stats_t *stats,
				uint16_t rpc_version, Buf buffer)
{
	int i=0;

	if (rpc_version >= SLURM_14_03_PROTOCOL_VERSION) {
		if (!stats) {
			for (i=0; i<3; i++)
				pack64(0, buffer);

			pack32(0, buffer);

			for (i=0; i<10; i++)
				packdouble(0, buffer);

			for (i=0; i<12; i++) {
				pack32(0, buffer);
			}
			return;
		}

		pack64(stats->vsize_max, buffer);
		pack64(stats->rss_max, buffer);
		pack64(stats->pages_max, buffer);
		pack32(stats->cpu_min, buffer);

		packdouble(stats->vsize_ave, buffer);
		packdouble(stats->rss_ave, buffer);
		packdouble(stats->pages_ave, buffer);
		packdouble(stats->cpu_ave, buffer);
		packdouble(stats->act_cpufreq, buffer);
		packdouble(stats->consumed_energy, buffer);
		packdouble(stats->disk_read_max, buffer);
		packdouble(stats->disk_read_ave, buffer);
		packdouble(stats->disk_write_max, buffer);
		packdouble(stats->disk_write_ave, buffer);

		pack32(stats->vsize_max_nodeid, buffer);
		pack32(stats->vsize_max_taskid, buffer);
		pack32(stats->rss_max_nodeid, buffer);
		pack32(stats->rss_max_taskid, buffer);
		pack32(stats->pages_max_nodeid, buffer);
		pack32(stats->pages_max_taskid, buffer);
		pack32(stats->cpu_min_nodeid, buffer);
		pack32(stats->cpu_min_taskid, buffer);
		pack32(stats->disk_read_max_nodeid, buffer);
		pack32(stats->disk_read_max_taskid, buffer);
		pack32(stats->disk_write_max_nodeid, buffer);
		pack32(stats->disk_write_max_taskid, buffer);
	} else if (rpc_version >= SLURMDBD_2_6_VERSION) {
		if (!stats) {
			for (i=0; i<4; i++)
				pack32((uint32_t) 0, buffer);

			for (i=0; i<10; i++)
				packdouble(0, buffer);

			for (i=0; i<12; i++) {
				pack32(0, buffer);
			}
			return;
		}

		pack32((uint32_t)stats->vsize_max, buffer);
		pack32((uint32_t)stats->rss_max, buffer);
		pack32((uint32_t)stats->pages_max, buffer);
		pack32(stats->cpu_min, buffer);

		packdouble(stats->vsize_ave, buffer);
		packdouble(stats->rss_ave, buffer);
		packdouble(stats->pages_ave, buffer);
		packdouble(stats->cpu_ave, buffer);
		packdouble(stats->act_cpufreq, buffer);
		packdouble(stats->consumed_energy, buffer);
		packdouble(stats->disk_read_max, buffer);
		packdouble(stats->disk_read_ave, buffer);
		packdouble(stats->disk_write_max, buffer);
		packdouble(stats->disk_write_ave, buffer);

		pack32(stats->vsize_max_nodeid, buffer);
		pack32(stats->vsize_max_taskid, buffer);
		pack32(stats->rss_max_nodeid, buffer);
		pack32(stats->rss_max_taskid, buffer);
		pack32(stats->pages_max_nodeid, buffer);
		pack32(stats->pages_max_taskid, buffer);
		pack32(stats->cpu_min_nodeid, buffer);
		pack32(stats->cpu_min_taskid, buffer);
		pack32(stats->disk_read_max_nodeid, buffer);
		pack32(stats->disk_read_max_taskid, buffer);
		pack32(stats->disk_write_max_nodeid, buffer);
		pack32(stats->disk_write_max_taskid, buffer);
	}
}

static int _unpack_slurmdb_stats(slurmdb_stats_t *stats,
				 uint16_t rpc_version, Buf buffer)
{
	if (rpc_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack64(&stats->vsize_max, buffer);
		safe_unpack64(&stats->rss_max, buffer);
		safe_unpack64(&stats->pages_max, buffer);
		safe_unpack32(&stats->cpu_min, buffer);

		safe_unpackdouble(&stats->vsize_ave, buffer);
		safe_unpackdouble(&stats->rss_ave, buffer);
		safe_unpackdouble(&stats->pages_ave, buffer);
		safe_unpackdouble(&stats->cpu_ave, buffer);
		safe_unpackdouble(&stats->act_cpufreq, buffer);
		safe_unpackdouble(&stats->consumed_energy, buffer);
		safe_unpackdouble(&stats->disk_read_max, buffer);
		safe_unpackdouble(&stats->disk_read_ave, buffer);
		safe_unpackdouble(&stats->disk_write_max, buffer);
		safe_unpackdouble(&stats->disk_write_ave, buffer);

		safe_unpack32(&stats->vsize_max_nodeid, buffer);
		safe_unpack32(&stats->vsize_max_taskid, buffer);
		safe_unpack32(&stats->rss_max_nodeid, buffer);
		safe_unpack32(&stats->rss_max_taskid, buffer);
		safe_unpack32(&stats->pages_max_nodeid, buffer);
		safe_unpack32(&stats->pages_max_taskid, buffer);
		safe_unpack32(&stats->cpu_min_nodeid, buffer);
		safe_unpack32(&stats->cpu_min_taskid, buffer);
		safe_unpack32(&stats->disk_read_max_nodeid, buffer);
		safe_unpack32(&stats->disk_read_max_taskid, buffer);
		safe_unpack32(&stats->disk_write_max_nodeid, buffer);
		safe_unpack32(&stats->disk_write_max_taskid, buffer);
	} else if (rpc_version >= SLURMDBD_2_6_VERSION) {
		safe_unpack32((uint32_t *)&stats->vsize_max, buffer);
		safe_unpack32((uint32_t *)&stats->rss_max, buffer);
		safe_unpack32((uint32_t *)&stats->pages_max, buffer);
		safe_unpack32(&stats->cpu_min, buffer);

		safe_unpackdouble(&stats->vsize_ave, buffer);
		safe_unpackdouble(&stats->rss_ave, buffer);
		safe_unpackdouble(&stats->pages_ave, buffer);
		safe_unpackdouble(&stats->cpu_ave, buffer);
		safe_unpackdouble(&stats->act_cpufreq, buffer);
		safe_unpackdouble(&stats->consumed_energy, buffer);
		safe_unpackdouble(&stats->disk_read_max, buffer);
		safe_unpackdouble(&stats->disk_read_ave, buffer);
		safe_unpackdouble(&stats->disk_write_max, buffer);
		safe_unpackdouble(&stats->disk_write_ave, buffer);

		safe_unpack32(&stats->vsize_max_nodeid, buffer);
		safe_unpack32(&stats->vsize_max_taskid, buffer);
		safe_unpack32(&stats->rss_max_nodeid, buffer);
		safe_unpack32(&stats->rss_max_taskid, buffer);
		safe_unpack32(&stats->pages_max_nodeid, buffer);
		safe_unpack32(&stats->pages_max_taskid, buffer);
		safe_unpack32(&stats->cpu_min_nodeid, buffer);
		safe_unpack32(&stats->cpu_min_taskid, buffer);
		safe_unpack32(&stats->disk_read_max_nodeid, buffer);
		safe_unpack32(&stats->disk_read_max_taskid, buffer);
		safe_unpack32(&stats->disk_write_max_nodeid, buffer);
		safe_unpack32(&stats->disk_write_max_taskid, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	memset(stats, 0, sizeof(slurmdb_stats_t));
       	return SLURM_ERROR;
}


extern void slurmdb_pack_user_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	ListIterator itr = NULL;
	slurmdb_user_rec_t *object = (slurmdb_user_rec_t *)in;
	uint32_t count = NO_VAL;
	slurmdb_coord_rec_t *coord = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	slurmdb_wckey_rec_t *wckey = NULL;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			pack16(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			pack32(0, buffer);
			pack32(NO_VAL, buffer);
			return;
		}

		pack16(object->admin_level, buffer);

		if (object->assoc_list)
			count = list_count(object->assoc_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->assoc_list);
			while ((assoc = list_next(itr))) {
				slurmdb_pack_association_rec(assoc, rpc_version,
							     buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->coord_accts)
			count = list_count(object->coord_accts);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->coord_accts);
			while ((coord = list_next(itr))) {
				slurmdb_pack_coord_rec(coord,
						       rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(object->default_acct, buffer);
		packstr(object->default_wckey, buffer);
		packstr(object->name, buffer);
		packstr(object->old_name, buffer);

		pack32(object->uid, buffer);

		if (object->wckey_list)
			count = list_count(object->wckey_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->wckey_list);
			while ((wckey = list_next(itr))) {
				slurmdb_pack_wckey_rec(wckey, rpc_version,
						       buffer);
			}
			list_iterator_destroy(itr);
		}
	}
}

extern int slurmdb_unpack_user_rec(void **object, uint16_t rpc_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_user_rec_t *object_ptr = xmalloc(sizeof(slurmdb_user_rec_t));
	uint32_t count = NO_VAL;
	slurmdb_coord_rec_t *coord = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	slurmdb_wckey_rec_t *wckey = NULL;
	int i;

	*object = object_ptr;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack16(&object_ptr->admin_level, buffer);
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->assoc_list =
				list_create(slurmdb_destroy_association_rec);
			for(i=0; i<count; i++) {
				if (slurmdb_unpack_association_rec(
					    (void *)&assoc, rpc_version, buffer)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->assoc_list, assoc);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->coord_accts =
				list_create(slurmdb_destroy_coord_rec);
			for(i=0; i<count; i++) {
				if (slurmdb_unpack_coord_rec(
					    (void *)&coord, rpc_version, buffer)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->coord_accts, coord);
			}
		}
		safe_unpackstr_xmalloc(&object_ptr->default_acct, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&object_ptr->default_wckey, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->old_name,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->uid, buffer);
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->wckey_list =
				list_create(slurmdb_destroy_wckey_rec);
			for(i=0; i<count; i++) {
				if (slurmdb_unpack_wckey_rec(
					    (void *)&wckey, rpc_version, buffer)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->wckey_list, wckey);
			}
		}

	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_user_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_used_limits(void *in, uint16_t rpc_version, Buf buffer)
{
	slurmdb_used_limits_t *object = (slurmdb_used_limits_t *)in;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			pack64(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			return;
		}

		pack64(object->cpu_run_mins, buffer);
		pack32(object->cpus, buffer);
		pack32(object->jobs, buffer);
		pack32(object->nodes, buffer);
		pack32(object->submit_jobs, buffer);
		pack32(object->uid, buffer);
	}
}

extern int slurmdb_unpack_used_limits(void **object,
				      uint16_t rpc_version, Buf buffer)
{
	slurmdb_used_limits_t *object_ptr =
		xmalloc(sizeof(slurmdb_used_limits_t));

	*object = (void *)object_ptr;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack64(&object_ptr->cpu_run_mins, buffer);
		safe_unpack32(&object_ptr->cpus, buffer);
		safe_unpack32(&object_ptr->jobs, buffer);
		safe_unpack32(&object_ptr->nodes, buffer);
		safe_unpack32(&object_ptr->submit_jobs, buffer);
		safe_unpack32(&object_ptr->uid, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_used_limits(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_account_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	slurmdb_coord_rec_t *coord = NULL;
	ListIterator itr = NULL;
	uint32_t count = NO_VAL;
	slurmdb_account_rec_t *object = (slurmdb_account_rec_t *)in;
	slurmdb_association_rec_t *assoc = NULL;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			return;
		}

		if (object->assoc_list)
			count = list_count(object->assoc_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->assoc_list);
			while ((assoc = list_next(itr))) {
				slurmdb_pack_association_rec(assoc, rpc_version,
							     buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->coordinators)
			count = list_count(object->coordinators);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->coordinators);
			while ((coord = list_next(itr))) {
				slurmdb_pack_coord_rec(coord,
						       rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}

		packstr(object->description, buffer);
		packstr(object->name, buffer);
		packstr(object->organization, buffer);
	}
}

extern int slurmdb_unpack_account_rec(void **object, uint16_t rpc_version,
				      Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_coord_rec_t *coord = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	slurmdb_account_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_account_rec_t));

	*object = object_ptr;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->assoc_list =
				list_create(slurmdb_destroy_association_rec);
			for(i=0; i<count; i++) {
				if (slurmdb_unpack_association_rec(
					    (void *)&assoc, rpc_version, buffer)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->assoc_list, assoc);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->coordinators =
				list_create(slurmdb_destroy_coord_rec);
			for(i=0; i<count; i++) {
				if (slurmdb_unpack_coord_rec(
					    (void *)&coord, rpc_version, buffer)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->coordinators, coord);
			}
		}
		safe_unpackstr_xmalloc(&object_ptr->description,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->organization,
				       &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_account_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_coord_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	slurmdb_coord_rec_t *object = (slurmdb_coord_rec_t *)in;

	if (!object) {
		packnull(buffer);
		pack16(0, buffer);
		return;
	}

	packstr(object->name, buffer);
	pack16(object->direct, buffer);
}

extern int slurmdb_unpack_coord_rec(void **object, uint16_t rpc_version,
				    Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_coord_rec_t *object_ptr = xmalloc(sizeof(slurmdb_coord_rec_t));

	*object = object_ptr;
	safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
	safe_unpack16(&object_ptr->direct, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_coord_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_cluster_accounting_rec(void *in, uint16_t rpc_version,
						Buf buffer)
{
	slurmdb_cluster_accounting_rec_t *object =
		(slurmdb_cluster_accounting_rec_t *)in;

	if (rpc_version >= SLURM_14_03_PROTOCOL_VERSION) {
		if (!object) {
			pack64(0, buffer);
			pack64(0, buffer);
			pack32(0, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack_time(0, buffer);
			pack64(0, buffer);
			return;
		}

		pack64(object->alloc_secs, buffer);
		pack64(object->consumed_energy, buffer);
		pack32(object->cpu_count, buffer);
		pack64(object->down_secs, buffer);
		pack64(object->idle_secs, buffer);
		pack64(object->over_secs, buffer);
		pack64(object->pdown_secs, buffer);
		pack_time(object->period_start, buffer);
		pack64(object->resv_secs, buffer);
	} else if (rpc_version >= SLURMDBD_2_6_VERSION) {
		if (!object) {
			pack64(0, buffer);
			pack32(0, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack_time(0, buffer);
			pack64(0, buffer);
			return;
		}

		pack64(object->alloc_secs, buffer);
		pack32(object->cpu_count, buffer);
		pack64(object->down_secs, buffer);
		pack64(object->idle_secs, buffer);
		pack64(object->over_secs, buffer);
		pack64(object->pdown_secs, buffer);
		pack_time(object->period_start, buffer);
		pack64(object->resv_secs, buffer);
	}
}

extern int slurmdb_unpack_cluster_accounting_rec(void **object,
						 uint16_t rpc_version,
						 Buf buffer)
{
	slurmdb_cluster_accounting_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_cluster_accounting_rec_t));

	*object = object_ptr;

	if (rpc_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack64(&object_ptr->alloc_secs, buffer);
		safe_unpack64(&object_ptr->consumed_energy, buffer);
		safe_unpack32(&object_ptr->cpu_count, buffer);
		safe_unpack64(&object_ptr->down_secs, buffer);
		safe_unpack64(&object_ptr->idle_secs, buffer);
		safe_unpack64(&object_ptr->over_secs, buffer);
		safe_unpack64(&object_ptr->pdown_secs, buffer);
		safe_unpack_time(&object_ptr->period_start, buffer);
		safe_unpack64(&object_ptr->resv_secs, buffer);
	} else if (rpc_version >= SLURMDBD_2_6_VERSION) {
		safe_unpack64(&object_ptr->alloc_secs, buffer);
		safe_unpack32(&object_ptr->cpu_count, buffer);
		safe_unpack64(&object_ptr->down_secs, buffer);
		safe_unpack64(&object_ptr->idle_secs, buffer);
		safe_unpack64(&object_ptr->over_secs, buffer);
		safe_unpack64(&object_ptr->pdown_secs, buffer);
		safe_unpack_time(&object_ptr->period_start, buffer);
		safe_unpack64(&object_ptr->resv_secs, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_cluster_accounting_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_clus_res_rec(void *in, uint16_t rpc_version,
				      Buf buffer)
{
	slurmdb_clus_res_rec_t *object = (slurmdb_clus_res_rec_t *)in;

	if (!object) {
		packnull(buffer);
		pack32(NO_VAL, buffer);
		return;
	}
	packstr(object->cluster, buffer);
	pack16(object->percent_allowed, buffer);
}

extern int slurmdb_unpack_clus_res_rec(void **object, uint16_t rpc_version,
				       Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_clus_res_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_clus_res_rec_t));

	*object = object_ptr;

	slurmdb_init_clus_res_rec(object_ptr, 0);
	safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp, buffer);
	safe_unpack16(&object_ptr->percent_allowed, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_clus_res_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_cluster_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	slurmdb_cluster_accounting_rec_t *slurmdb_info = NULL;
	ListIterator itr = NULL;
	uint32_t count = NO_VAL;
	slurmdb_cluster_rec_t *object = (slurmdb_cluster_rec_t *)in;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			packnull(buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack16(1, buffer);
			pack32(NO_VAL, buffer);

			packnull(buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);

			slurmdb_pack_association_rec(NULL, rpc_version, buffer);

			pack16(0, buffer);
			return;
		}

		if (object->accounting_list)
			count = list_count(object->accounting_list);

		pack32(count, buffer);

		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->accounting_list);
			while ((slurmdb_info = list_next(itr))) {
				slurmdb_pack_cluster_accounting_rec(
					slurmdb_info, rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->classification, buffer);
		packstr(object->control_host, buffer);
		pack32(object->control_port, buffer);
		pack32(object->cpu_count, buffer);
		pack16(object->dimensions, buffer);
		pack32(object->flags, buffer);

		packstr(object->name, buffer);
		packstr(object->nodes, buffer);

		pack32(object->plugin_id_select, buffer);

		slurmdb_pack_association_rec(object->root_assoc,
					     rpc_version, buffer);

		pack16(object->rpc_version, buffer);
	}
}

extern int slurmdb_unpack_cluster_rec(void **object, uint16_t rpc_version,
				      Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_cluster_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_cluster_rec_t));
	slurmdb_cluster_accounting_rec_t *slurmdb_info = NULL;

	*object = object_ptr;

	slurmdb_init_cluster_rec(object_ptr, 0);
	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->accounting_list = list_create(
				slurmdb_destroy_cluster_accounting_rec);
			for(i=0; i<count; i++) {
				if (slurmdb_unpack_cluster_accounting_rec(
					    (void *)&slurmdb_info,
					    rpc_version, buffer) == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->accounting_list,
					    slurmdb_info);
			}
		}

		safe_unpack16(&object_ptr->classification, buffer);
		safe_unpackstr_xmalloc(&object_ptr->control_host,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->control_port, buffer);
		safe_unpack32(&object_ptr->cpu_count, buffer);
		safe_unpack16(&object_ptr->dimensions, buffer);
		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->nodes, &uint32_tmp, buffer);

		safe_unpack32(&object_ptr->plugin_id_select, buffer);

		if (slurmdb_unpack_association_rec(
			    (void **)&object_ptr->root_assoc,
			    rpc_version, buffer)
		    == SLURM_ERROR)
			goto unpack_error;

		safe_unpack16(&object_ptr->rpc_version, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_cluster_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_accounting_rec(void *in, uint16_t rpc_version,
					Buf buffer)
{
	slurmdb_accounting_rec_t *object = (slurmdb_accounting_rec_t *)in;

	if (rpc_version >= SLURM_14_03_PROTOCOL_VERSION) {
		if (!object) {
			pack64(0, buffer);
			pack64(0, buffer);
			pack32(0, buffer);
			pack_time(0, buffer);
			return;
		}

		pack64(object->alloc_secs, buffer);
		pack64(object->consumed_energy, buffer);
		pack32(object->id, buffer);
		pack_time(object->period_start, buffer);
	} else if (rpc_version >= SLURMDBD_2_6_VERSION) {
		if (!object) {
			pack64(0, buffer);
			pack32(0, buffer);
			pack_time(0, buffer);
			return;
		}

		pack64(object->alloc_secs, buffer);
		pack32(object->id, buffer);
		pack_time(object->period_start, buffer);
	}
}

extern int slurmdb_unpack_accounting_rec(void **object, uint16_t rpc_version,
					 Buf buffer)
{
	slurmdb_accounting_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_accounting_rec_t));

	*object = object_ptr;

	if (rpc_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack64(&object_ptr->alloc_secs, buffer);
		safe_unpack64(&object_ptr->consumed_energy, buffer);
		safe_unpack32(&object_ptr->id, buffer);
		safe_unpack_time(&object_ptr->period_start, buffer);
	} else if (rpc_version >= SLURMDBD_2_6_VERSION) {
		safe_unpack64(&object_ptr->alloc_secs, buffer);
		safe_unpack32(&object_ptr->id, buffer);
		safe_unpack_time(&object_ptr->period_start, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_accounting_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_association_rec(void *in, uint16_t rpc_version,
					 Buf buffer)
{
	slurmdb_accounting_rec_t *slurmdb_info = NULL;
	ListIterator itr = NULL;
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	slurmdb_association_rec_t *object = (slurmdb_association_rec_t *)in;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);

			pack64(NO_VAL, buffer);
			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(0, buffer);
			pack16(0, buffer);
			pack32(0, buffer);

			pack64(NO_VAL, buffer);
			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			packnull(buffer);
			pack32(0, buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);

			pack32(0, buffer);
			pack32(0, buffer);

			packnull(buffer);
			return;
		}

		if (object->accounting_list)
			count = list_count(object->accounting_list);

		pack32(count, buffer);

		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->accounting_list);
			while ((slurmdb_info = list_next(itr))) {
				slurmdb_pack_accounting_rec(slurmdb_info,
							    rpc_version,
							    buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		packstr(object->acct, buffer);
		packstr(object->cluster, buffer);

		pack32(object->def_qos_id, buffer);

		/* this used to be named fairshare to not have to redo
		   the order of things just to be in alpha order we
		   just renamed it and called it good */
		pack32(object->shares_raw, buffer);

		pack64(object->grp_cpu_mins, buffer);
		pack64(object->grp_cpu_run_mins, buffer);
		pack32(object->grp_cpus, buffer);
		pack32(object->grp_jobs, buffer);
		pack32(object->grp_mem, buffer);
		pack32(object->grp_nodes, buffer);
		pack32(object->grp_submit_jobs, buffer);
		pack32(object->grp_wall, buffer);

		pack32(object->id, buffer);
		pack16(object->is_def, buffer);
		pack32(object->lft, buffer);

		pack64(object->max_cpu_mins_pj, buffer);
		pack64(object->max_cpu_run_mins, buffer);
		pack32(object->max_cpus_pj, buffer);
		pack32(object->max_jobs, buffer);
		pack32(object->max_nodes_pj, buffer);
		pack32(object->max_submit_jobs, buffer);
		pack32(object->max_wall_pj, buffer);

		packstr(object->parent_acct, buffer);
		pack32(object->parent_id, buffer);
		packstr(object->partition, buffer);

		if (object->qos_list)
			count = list_count(object->qos_list);

		pack32(count, buffer);

		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->qos_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack32(object->rgt, buffer);
		pack32(object->uid, buffer);

		packstr(object->user, buffer);
	}
}

extern int slurmdb_unpack_association_rec(void **object, uint16_t rpc_version,
					  Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	char *tmp_info = NULL;
	slurmdb_association_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_association_rec_t));
	slurmdb_accounting_rec_t *slurmdb_info = NULL;

	*object = object_ptr;

	slurmdb_init_association_rec(object_ptr, 0);

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->accounting_list =
				list_create(slurmdb_destroy_accounting_rec);
			for(i=0; i<count; i++) {
				if (slurmdb_unpack_accounting_rec(
					    (void **)&slurmdb_info,
					    rpc_version,
					    buffer) == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->accounting_list,
					    slurmdb_info);
			}
		}

		safe_unpackstr_xmalloc(&object_ptr->acct, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp,
				       buffer);

		safe_unpack32(&object_ptr->def_qos_id, buffer);

		safe_unpack32(&object_ptr->shares_raw, buffer);

		safe_unpack64(&object_ptr->grp_cpu_mins, buffer);
		safe_unpack64(&object_ptr->grp_cpu_run_mins, buffer);
		safe_unpack32(&object_ptr->grp_cpus, buffer);
		safe_unpack32(&object_ptr->grp_jobs, buffer);
		safe_unpack32(&object_ptr->grp_mem, buffer);
		safe_unpack32(&object_ptr->grp_nodes, buffer);
		safe_unpack32(&object_ptr->grp_submit_jobs, buffer);
		safe_unpack32(&object_ptr->grp_wall, buffer);

		safe_unpack32(&object_ptr->id, buffer);
		safe_unpack16(&object_ptr->is_def, buffer);
		safe_unpack32(&object_ptr->lft, buffer);

		safe_unpack64(&object_ptr->max_cpu_mins_pj, buffer);
		safe_unpack64(&object_ptr->max_cpu_run_mins, buffer);
		safe_unpack32(&object_ptr->max_cpus_pj, buffer);
		safe_unpack32(&object_ptr->max_jobs, buffer);
		safe_unpack32(&object_ptr->max_nodes_pj, buffer);
		safe_unpack32(&object_ptr->max_submit_jobs, buffer);
		safe_unpack32(&object_ptr->max_wall_pj, buffer);

		safe_unpackstr_xmalloc(&object_ptr->parent_acct, &uint32_tmp,
				       buffer);
		safe_unpack32(&object_ptr->parent_id, buffer);
		safe_unpackstr_xmalloc(&object_ptr->partition, &uint32_tmp,
				       buffer);

		safe_unpack32(&count, buffer);
		/* This needs to look for zero to tell if something
		   has changed */
		if (count != NO_VAL) {
			object_ptr->qos_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->qos_list, tmp_info);
			}
		}

		safe_unpack32(&object_ptr->rgt, buffer);
		safe_unpack32(&object_ptr->uid, buffer);

		safe_unpackstr_xmalloc(&object_ptr->user, &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_association_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_event_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	slurmdb_event_rec_t *object = (slurmdb_event_rec_t *)in;

	if (!object) {
		packnull(buffer);
		packnull(buffer);
		pack32(NO_VAL, buffer);
		pack16(0, buffer);
		packnull(buffer);
		pack_time(0, buffer);
		pack_time(0, buffer);
		packnull(buffer);
		pack32(NO_VAL, buffer);
		pack16((uint16_t)NO_VAL, buffer);
		return;
	}

	packstr(object->cluster, buffer);
	packstr(object->cluster_nodes, buffer);
	pack32(object->cpu_count, buffer);
	pack16(object->event_type, buffer);
	packstr(object->node_name, buffer);
	pack_time(object->period_start, buffer);
	pack_time(object->period_end, buffer);
	packstr(object->reason, buffer);
	pack32(object->reason_uid, buffer);
	pack16(object->state, buffer);
}

extern int slurmdb_unpack_event_rec(void **object, uint16_t rpc_version,
				    Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_event_rec_t *object_ptr = xmalloc(sizeof(slurmdb_event_rec_t));

	*object = object_ptr;

	safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&object_ptr->cluster_nodes, &uint32_tmp, buffer);
	safe_unpack32(&object_ptr->cpu_count, buffer);
	safe_unpack16(&object_ptr->event_type, buffer);
	safe_unpackstr_xmalloc(&object_ptr->node_name, &uint32_tmp, buffer);
	safe_unpack_time(&object_ptr->period_start, buffer);
	safe_unpack_time(&object_ptr->period_end, buffer);
	safe_unpackstr_xmalloc(&object_ptr->reason, &uint32_tmp, buffer);
	safe_unpack32(&object_ptr->reason_uid, buffer);
	safe_unpack16(&object_ptr->state, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_event_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_qos_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	ListIterator itr = NULL;
	slurmdb_qos_rec_t *object = (slurmdb_qos_rec_t *)in;
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;

	if (rpc_version >= SLURM_14_11_PROTOCOL_VERSION) {
		if (!object) {
			packnull(buffer);
			pack32(0, buffer);

			pack32(QOS_FLAG_NOTSET, buffer);

			pack32(NO_VAL, buffer);
			pack64(NO_VAL, buffer);
			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack64(NO_VAL, buffer);
			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			packnull(buffer);

			pack_bit_str_hex(NULL, buffer);
			pack32(NO_VAL, buffer);

			pack16(0, buffer);
			pack32(0, buffer);

			packdouble((double)NO_VAL, buffer);
			packdouble((double)NO_VAL, buffer);
			return;
		}
		packstr(object->description, buffer);
		pack32(object->id, buffer);

		pack32(object->flags, buffer);

		pack32(object->grace_time, buffer);
		pack64(object->grp_cpu_mins, buffer);
		pack64(object->grp_cpu_run_mins, buffer);
		pack32(object->grp_cpus, buffer);
		pack32(object->grp_jobs, buffer);
		pack32(object->grp_mem, buffer);
		pack32(object->grp_nodes, buffer);
		pack32(object->grp_submit_jobs, buffer);
		pack32(object->grp_wall, buffer);

		pack64(object->max_cpu_mins_pj, buffer);
		pack64(object->max_cpu_run_mins_pu, buffer);
		pack32(object->max_cpus_pj, buffer);
		pack32(object->max_cpus_pu, buffer);
		pack32(object->max_jobs_pu, buffer);
		pack32(object->max_nodes_pj, buffer);
		pack32(object->max_nodes_pu, buffer);
		pack32(object->max_submit_jobs_pu, buffer);
		pack32(object->max_wall_pj, buffer);
		pack32(object->min_cpus_pj, buffer);

		packstr(object->name, buffer);

		pack_bit_str_hex(object->preempt_bitstr, buffer);

		if (object->preempt_list)
			count = list_count(object->preempt_list);

		pack32(count, buffer);

		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->preempt_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->preempt_mode, buffer);
		pack32(object->priority, buffer);

		packdouble(object->usage_factor, buffer);
		packdouble(object->usage_thres, buffer);
	} else if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			packnull(buffer);
			pack32(0, buffer);

			pack32(QOS_FLAG_NOTSET, buffer);

			pack32(NO_VAL, buffer);
			pack64(NO_VAL, buffer);
			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack64(NO_VAL, buffer);
			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			packnull(buffer);

			pack_bit_str(NULL, buffer);
			pack32(NO_VAL, buffer);

			pack16(0, buffer);
			pack32(0, buffer);

			packdouble((double)NO_VAL, buffer);
			packdouble((double)NO_VAL, buffer);
			return;
		}
		packstr(object->description, buffer);
		pack32(object->id, buffer);

		pack32(object->flags, buffer);

		pack32(object->grace_time, buffer);
		pack64(object->grp_cpu_mins, buffer);
		pack64(object->grp_cpu_run_mins, buffer);
		pack32(object->grp_cpus, buffer);
		pack32(object->grp_jobs, buffer);
		pack32(object->grp_mem, buffer);
		pack32(object->grp_nodes, buffer);
		pack32(object->grp_submit_jobs, buffer);
		pack32(object->grp_wall, buffer);

		pack64(object->max_cpu_mins_pj, buffer);
		pack64(object->max_cpu_run_mins_pu, buffer);
		pack32(object->max_cpus_pj, buffer);
		pack32(object->max_cpus_pu, buffer);
		pack32(object->max_jobs_pu, buffer);
		pack32(object->max_nodes_pj, buffer);
		pack32(object->max_nodes_pu, buffer);
		pack32(object->max_submit_jobs_pu, buffer);
		pack32(object->max_wall_pj, buffer);

		packstr(object->name, buffer);

		pack_bit_str(object->preempt_bitstr, buffer);

		if (object->preempt_list)
			count = list_count(object->preempt_list);

		pack32(count, buffer);

		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->preempt_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->preempt_mode, buffer);
		pack32(object->priority, buffer);

		packdouble(object->usage_factor, buffer);
		packdouble(object->usage_thres, buffer);
	}
}

extern int slurmdb_unpack_qos_rec(void **object, uint16_t rpc_version,
				  Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	slurmdb_qos_rec_t *object_ptr = xmalloc(sizeof(slurmdb_qos_rec_t));
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;

	*object = object_ptr;

	slurmdb_init_qos_rec(object_ptr, 0);

	if (rpc_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object_ptr->description,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->id, buffer);

		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpack32(&object_ptr->grace_time, buffer);
		safe_unpack64(&object_ptr->grp_cpu_mins, buffer);
		safe_unpack64(&object_ptr->grp_cpu_run_mins, buffer);
		safe_unpack32(&object_ptr->grp_cpus, buffer);
		safe_unpack32(&object_ptr->grp_jobs, buffer);
		safe_unpack32(&object_ptr->grp_mem, buffer);
		safe_unpack32(&object_ptr->grp_nodes, buffer);
		safe_unpack32(&object_ptr->grp_submit_jobs, buffer);
		safe_unpack32(&object_ptr->grp_wall, buffer);

		safe_unpack64(&object_ptr->max_cpu_mins_pj, buffer);
		safe_unpack64(&object_ptr->max_cpu_run_mins_pu, buffer);
		safe_unpack32(&object_ptr->max_cpus_pj, buffer);
		safe_unpack32(&object_ptr->max_cpus_pu, buffer);
		safe_unpack32(&object_ptr->max_jobs_pu, buffer);
		safe_unpack32(&object_ptr->max_nodes_pj, buffer);
		safe_unpack32(&object_ptr->max_nodes_pu, buffer);
		safe_unpack32(&object_ptr->max_submit_jobs_pu, buffer);
		safe_unpack32(&object_ptr->max_wall_pj, buffer);
		safe_unpack32(&object_ptr->min_cpus_pj, buffer);

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);

		unpack_bit_str_hex(&object_ptr->preempt_bitstr, buffer);

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->preempt_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->preempt_list,
					    tmp_info);
			}
		}

		safe_unpack16(&object_ptr->preempt_mode, buffer);
		safe_unpack32(&object_ptr->priority, buffer);

		safe_unpackdouble(&object_ptr->usage_factor, buffer);
		safe_unpackdouble(&object_ptr->usage_thres, buffer);
	} else if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpackstr_xmalloc(&object_ptr->description,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->id, buffer);

		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpack32(&object_ptr->grace_time, buffer);
		safe_unpack64(&object_ptr->grp_cpu_mins, buffer);
		safe_unpack64(&object_ptr->grp_cpu_run_mins, buffer);
		safe_unpack32(&object_ptr->grp_cpus, buffer);
		safe_unpack32(&object_ptr->grp_jobs, buffer);
		safe_unpack32(&object_ptr->grp_mem, buffer);
		safe_unpack32(&object_ptr->grp_nodes, buffer);
		safe_unpack32(&object_ptr->grp_submit_jobs, buffer);
		safe_unpack32(&object_ptr->grp_wall, buffer);

		safe_unpack64(&object_ptr->max_cpu_mins_pj, buffer);
		safe_unpack64(&object_ptr->max_cpu_run_mins_pu, buffer);
		safe_unpack32(&object_ptr->max_cpus_pj, buffer);
		safe_unpack32(&object_ptr->max_cpus_pu, buffer);
		safe_unpack32(&object_ptr->max_jobs_pu, buffer);
		safe_unpack32(&object_ptr->max_nodes_pj, buffer);
		safe_unpack32(&object_ptr->max_nodes_pu, buffer);
		safe_unpack32(&object_ptr->max_submit_jobs_pu, buffer);
		safe_unpack32(&object_ptr->max_wall_pj, buffer);

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);

		unpack_bit_str(&object_ptr->preempt_bitstr, buffer);

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->preempt_list =
				list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->preempt_list,
					    tmp_info);
			}
		}

		safe_unpack16(&object_ptr->preempt_mode, buffer);
		safe_unpack32(&object_ptr->priority, buffer);

		safe_unpackdouble(&object_ptr->usage_factor, buffer);
		safe_unpackdouble(&object_ptr->usage_thres, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_qos_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_reservation_rec(void *in, uint16_t rpc_version,
					 Buf buffer)
{
	slurmdb_reservation_rec_t *object = (slurmdb_reservation_rec_t *)in;

	if (rpc_version >= SLURM_14_03_PROTOCOL_VERSION) {
		if (!object) {
			pack64(0, buffer);
			packnull(buffer);
			packnull(buffer);
			pack32((uint32_t)NO_VAL, buffer);
			pack64(0, buffer);
			pack32((uint32_t)NO_VAL, buffer);
			pack32(0, buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			return;
		}

		pack64(object->alloc_secs, buffer);
		packstr(object->assocs, buffer);
		packstr(object->cluster, buffer);
		pack32(object->cpus, buffer);
		pack64(object->down_secs, buffer);
		pack32(object->flags, buffer);
		pack32(object->id, buffer);
		packstr(object->name, buffer);
		packstr(object->nodes, buffer);
		packstr(object->node_inx, buffer);
		pack_time(object->time_end, buffer);
		pack_time(object->time_start, buffer);
		pack_time(object->time_start_prev, buffer);
	} else if (rpc_version >= SLURMDBD_2_6_VERSION) {
		if (!object) {
			pack64(0, buffer);
			packnull(buffer);
			packnull(buffer);
			pack32((uint32_t)NO_VAL, buffer);
			pack64(0, buffer);
			pack16((uint16_t)NO_VAL, buffer);
			pack32(0, buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			return;
		}

		pack64(object->alloc_secs, buffer);
		packstr(object->assocs, buffer);
		packstr(object->cluster, buffer);
		pack32(object->cpus, buffer);
		pack64(object->down_secs, buffer);
		pack16((uint16_t)object->flags, buffer);
		pack32(object->id, buffer);
		packstr(object->name, buffer);
		packstr(object->nodes, buffer);
		packstr(object->node_inx, buffer);
		pack_time(object->time_end, buffer);
		pack_time(object->time_start, buffer);
		pack_time(object->time_start_prev, buffer);
	}
}

extern int slurmdb_unpack_reservation_rec(void **object, uint16_t rpc_version,
					  Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_reservation_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_reservation_rec_t));

	*object = object_ptr;

	if (rpc_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack64(&object_ptr->alloc_secs, buffer);
		safe_unpackstr_xmalloc(&object_ptr->assocs, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp,
				       buffer);
		safe_unpack32(&object_ptr->cpus, buffer);
		safe_unpack64(&object_ptr->down_secs, buffer);
		safe_unpack32(&object_ptr->flags, buffer);
		safe_unpack32(&object_ptr->id, buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->node_inx, &uint32_tmp,
				       buffer);
		safe_unpack_time(&object_ptr->time_end, buffer);
		safe_unpack_time(&object_ptr->time_start, buffer);
		safe_unpack_time(&object_ptr->time_start_prev, buffer);
	} else if (rpc_version >= SLURMDBD_2_6_VERSION) {
		uint16_t flags;
		safe_unpack64(&object_ptr->alloc_secs, buffer);
		safe_unpackstr_xmalloc(&object_ptr->assocs, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp,
				       buffer);
		safe_unpack32(&object_ptr->cpus, buffer);
		safe_unpack64(&object_ptr->down_secs, buffer);
		safe_unpack16(&flags, buffer);
		object_ptr->flags = flags;
		safe_unpack32(&object_ptr->id, buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->node_inx, &uint32_tmp,
				       buffer);
		safe_unpack_time(&object_ptr->time_end, buffer);
		safe_unpack_time(&object_ptr->time_start, buffer);
		safe_unpack_time(&object_ptr->time_start_prev, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_reservation_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}


extern void slurmdb_pack_res_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	slurmdb_res_rec_t *object = (slurmdb_res_rec_t *)in;
	uint32_t count = NO_VAL;
	slurmdb_clus_res_rec_t *clus_res;
	ListIterator itr;

	if (!object) {
		pack32(NO_VAL, buffer); // clus_res_list
		pack32(NO_VAL, buffer); // clus_res_rec
		pack32(NO_VAL, buffer); // count
		packnull(buffer); // description
		pack32(SLURMDB_RES_FLAG_NOTSET, buffer); // flags
		pack32(NO_VAL, buffer); // id
		packnull(buffer); // manager
		packnull(buffer); // name
		pack16(0, buffer); // percent_used
		packnull(buffer); // server
		pack32(SLURMDB_RESOURCE_NOTSET, buffer); // type

		return;
	}
	if (object->clus_res_list)
		count = list_count(object->clus_res_list);

	pack32(count, buffer);

	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->clus_res_list);
		while ((clus_res = list_next(itr)))
			slurmdb_pack_clus_res_rec(
				clus_res, rpc_version, buffer);
		list_iterator_destroy(itr);
	}

	if (object->clus_res_rec) {
		pack32(0, buffer); /* anything not NO_VAL */
		slurmdb_pack_clus_res_rec(
			object->clus_res_rec, rpc_version, buffer);
	} else
		pack32(NO_VAL, buffer);

	pack32(object->count, buffer);
	packstr(object->description, buffer);
	pack32(object->flags, buffer);
	pack32(object->id, buffer);
	packstr(object->manager, buffer);
	packstr(object->name, buffer);
	pack16(object->percent_used, buffer);
	packstr(object->server, buffer);
	pack32(object->type, buffer);
}

extern int slurmdb_unpack_res_rec(void **object, uint16_t rpc_version,
				  Buf buffer)
{
	uint32_t uint32_tmp;
	uint32_t count;
	int i;
	slurmdb_res_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_res_rec_t));
	slurmdb_clus_res_rec_t *clus_res;

	*object = object_ptr;

	slurmdb_init_res_rec(object_ptr, 0);

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->clus_res_list =
			list_create(slurmdb_destroy_clus_res_rec);
		for (i=0; i<count; i++) {
			if (slurmdb_unpack_clus_res_rec(
				    (void **)&clus_res, rpc_version, buffer)
			    != SLURM_SUCCESS)
				goto unpack_error;
			list_append(object_ptr->clus_res_list, clus_res);
		}
	}

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		if (slurmdb_unpack_clus_res_rec(
			    (void **)&object_ptr->clus_res_rec,
			    rpc_version, buffer) != SLURM_SUCCESS)
			goto unpack_error;
	}

	safe_unpack32(&object_ptr->count, buffer);
	safe_unpackstr_xmalloc(&object_ptr->description, &uint32_tmp, buffer);
	safe_unpack32(&object_ptr->flags, buffer);
	safe_unpack32(&object_ptr->id, buffer);
	safe_unpackstr_xmalloc(&object_ptr->manager, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
	safe_unpack16(&object_ptr->percent_used, buffer);
	safe_unpackstr_xmalloc(&object_ptr->server, &uint32_tmp, buffer);
	safe_unpack32(&object_ptr->type, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_res_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_txn_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	slurmdb_txn_rec_t *object = (slurmdb_txn_rec_t *)in;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			packnull(buffer);
			pack16(0, buffer);
			packnull(buffer);
			packnull(buffer);
			pack32(0, buffer);
			packnull(buffer);
			pack_time(0, buffer);
			packnull(buffer);
			packnull(buffer);
			return;
		}

		packstr(object->accts, buffer);
		pack16(object->action, buffer);
		packstr(object->actor_name, buffer);
		packstr(object->clusters, buffer);
		pack32(object->id, buffer);
		packstr(object->set_info, buffer);
		pack_time(object->timestamp, buffer);
		packstr(object->users, buffer);
		packstr(object->where_query, buffer);
	}
}

extern int slurmdb_unpack_txn_rec(
	void **object, uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_txn_rec_t *object_ptr = xmalloc(sizeof(slurmdb_txn_rec_t));

	*object = object_ptr;
	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpackstr_xmalloc(&object_ptr->accts,
				       &uint32_tmp, buffer);
		safe_unpack16(&object_ptr->action, buffer);
		safe_unpackstr_xmalloc(&object_ptr->actor_name,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->clusters,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->id, buffer);
		safe_unpackstr_xmalloc(&object_ptr->set_info,
				       &uint32_tmp, buffer);
		safe_unpack_time(&object_ptr->timestamp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->users,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->where_query,
				       &uint32_tmp, buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_txn_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;

}

extern void slurmdb_pack_wckey_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	slurmdb_accounting_rec_t *slurmdb_info = NULL;
	ListIterator itr = NULL;
	uint32_t count = NO_VAL;
	slurmdb_wckey_rec_t *object = (slurmdb_wckey_rec_t *)in;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);

			packnull(buffer);

			pack32(NO_VAL, buffer);

			packnull(buffer);

			pack32(NO_VAL, buffer);

			packnull(buffer);
			return;
		}

		if (object->accounting_list)
			count = list_count(object->accounting_list);

		pack32(count, buffer);

		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->accounting_list);
			while ((slurmdb_info = list_next(itr))) {
				slurmdb_pack_accounting_rec(
					slurmdb_info, rpc_version, buffer);
			}
			list_iterator_destroy(itr);
		}

		packstr(object->cluster, buffer);

		pack32(object->id, buffer);

		pack16(object->is_def, buffer);

		packstr(object->name, buffer);

		pack32(object->uid, buffer);

		packstr(object->user, buffer);
	}
}

extern int slurmdb_unpack_wckey_rec(void **object, uint16_t rpc_version,
				    Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_wckey_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_wckey_rec_t));
	slurmdb_accounting_rec_t *slurmdb_info = NULL;

	*object = object_ptr;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->accounting_list =
				list_create(slurmdb_destroy_accounting_rec);
			for(i=0; i<count; i++) {
				if (slurmdb_unpack_accounting_rec(
					    (void **)&slurmdb_info,
					    rpc_version,
					    buffer) == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->accounting_list,
					    slurmdb_info);
			}
		}

		safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp,
				       buffer);

		safe_unpack32(&object_ptr->id, buffer);

		safe_unpack16(&object_ptr->is_def, buffer);

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);

		safe_unpack32(&object_ptr->uid, buffer);

		safe_unpackstr_xmalloc(&object_ptr->user, &uint32_tmp, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_wckey_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_archive_rec(void *in, uint16_t rpc_version, Buf buffer)
{
	slurmdb_archive_rec_t *object = (slurmdb_archive_rec_t *)in;

	if (!object) {
		packnull(buffer);
		packnull(buffer);
		return;
	}

	packstr(object->archive_file, buffer);
	packstr(object->insert, buffer);
}

extern int slurmdb_unpack_archive_rec(void **object, uint16_t rpc_version,
				      Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_archive_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_archive_rec_t));

	*object = object_ptr;

	safe_unpackstr_xmalloc(&object_ptr->archive_file, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&object_ptr->insert, &uint32_tmp, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_archive_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;

}

extern void slurmdb_pack_user_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	slurmdb_user_cond_t *object = (slurmdb_user_cond_t *)in;
	uint32_t count = NO_VAL;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			pack16(0, buffer);
			slurmdb_pack_association_cond(
				NULL, rpc_version, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}

		pack16(object->admin_level, buffer);

		slurmdb_pack_association_cond(object->assoc_cond,
					      rpc_version, buffer);

		if (object->def_acct_list)
			count = list_count(object->def_acct_list);

		pack32(count, buffer);

		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->def_acct_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->def_wckey_list)
			count = list_count(object->def_wckey_list);

		pack32(count, buffer);

		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->def_wckey_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->with_assocs, buffer);
		pack16(object->with_coords, buffer);
		pack16(object->with_deleted, buffer);
		pack16(object->with_wckeys, buffer);
	}
}

extern int slurmdb_unpack_user_cond(void **object, uint16_t rpc_version,
				    Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_user_cond_t *object_ptr = xmalloc(sizeof(slurmdb_user_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack16(&object_ptr->admin_level, buffer);

		if (slurmdb_unpack_association_cond(
			    (void **)&object_ptr->assoc_cond,
			    rpc_version, buffer) == SLURM_ERROR)
			goto unpack_error;

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			if (!object_ptr->def_acct_list)
				object_ptr->def_acct_list =
					list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(
					&tmp_info, &uint32_tmp, buffer);
				list_append(object_ptr->def_acct_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->def_wckey_list =
				list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->def_wckey_list,
					    tmp_info);
			}
		}
		safe_unpack16(&object_ptr->with_assocs, buffer);
		safe_unpack16(&object_ptr->with_coords, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
		safe_unpack16(&object_ptr->with_wckeys, buffer);

		/* If we get the call from an older version of SLURM
		   just automatically set this to get the defaults */
		if (rpc_version < 8) {
			if (!object_ptr->with_assocs)
				object_ptr->assoc_cond->only_defs = 1;
			else
				object_ptr->with_wckeys = 1;
		}
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_user_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_account_cond(void *in, uint16_t rpc_version,
				      Buf buffer)
{
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	slurmdb_account_cond_t *object = (slurmdb_account_cond_t *)in;
	uint32_t count = NO_VAL;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			slurmdb_pack_association_cond(NULL, rpc_version,
						      buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}
		slurmdb_pack_association_cond(object->assoc_cond,
					      rpc_version, buffer);

		count = NO_VAL;
		if (object->description_list)
			count = list_count(object->description_list);

		pack32(count, buffer);

		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->description_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->organization_list)
			count = list_count(object->organization_list);

		pack32(count, buffer);

		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->organization_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->with_assocs, buffer);
		pack16(object->with_coords, buffer);
		pack16(object->with_deleted, buffer);
	}
}

extern int slurmdb_unpack_account_cond(void **object, uint16_t rpc_version,
				       Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_account_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_account_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (slurmdb_unpack_association_cond(
			    (void **)&object_ptr->assoc_cond,
			    rpc_version, buffer) == SLURM_ERROR)
			goto unpack_error;

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->description_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->description_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->organization_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->organization_list,
					    tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_assocs, buffer);
		safe_unpack16(&object_ptr->with_coords, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_account_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_cluster_cond(void *in, uint16_t rpc_version,
				      Buf buffer)
{
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	slurmdb_cluster_cond_t *object = (slurmdb_cluster_cond_t *)in;
	uint32_t count = NO_VAL;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			pack16(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}

		pack16(object->classification, buffer);

		if (object->cluster_list)
			count = list_count(object->cluster_list);

		pack32(count, buffer);

		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack32(object->flags, buffer);

		if (object->plugin_id_select_list)
			count = list_count(object->plugin_id_select_list);

		pack32(count, buffer);

		if (count && count != NO_VAL) {
			itr = list_iterator_create(
				object->plugin_id_select_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->rpc_version_list)
			count = list_count(object->rpc_version_list);

		pack32(count, buffer);

		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->rpc_version_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack_time(object->usage_end, buffer);
		pack_time(object->usage_start, buffer);

		pack16(object->with_usage, buffer);
		pack16(object->with_deleted, buffer);
	}
}

extern int slurmdb_unpack_cluster_cond(void **object, uint16_t rpc_version,
				       Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_cluster_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_cluster_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	slurmdb_init_cluster_cond(object_ptr, 0);
	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack16(&object_ptr->classification, buffer);
		safe_unpack32(&count, buffer);
		if (count && count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpack32(&count, buffer);
		if (count && count != NO_VAL) {
			object_ptr->plugin_id_select_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->plugin_id_select_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count && count != NO_VAL) {
			object_ptr->rpc_version_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->rpc_version_list,
					    tmp_info);
			}
		}

		safe_unpack_time(&object_ptr->usage_end, buffer);
		safe_unpack_time(&object_ptr->usage_start, buffer);

		safe_unpack16(&object_ptr->with_usage, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_cluster_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_association_cond(void *in, uint16_t rpc_version,
					  Buf buffer)
{
	char *tmp_info = NULL;
	uint32_t count = NO_VAL;

	ListIterator itr = NULL;
	slurmdb_association_cond_t *object = (slurmdb_association_cond_t *)in;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack16(0, buffer);

			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);

			pack_time(0, buffer);
			pack_time(0, buffer);

			pack32(NO_VAL, buffer);

			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}

		if (object->acct_list)
			count = list_count(object->acct_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->acct_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->cluster_list)
			count = list_count(object->cluster_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->def_qos_id_list)
			count = list_count(object->def_qos_id_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->def_qos_id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->fairshare_list)
			count = list_count(object->fairshare_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->fairshare_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->grp_cpu_mins_list)
			count = list_count(object->grp_cpu_mins_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_cpu_mins_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->grp_cpu_run_mins_list)
			count = list_count(object->grp_cpu_run_mins_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(
				object->grp_cpu_run_mins_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->grp_cpus_list)
			count = list_count(object->grp_cpus_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_cpus_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->grp_jobs_list)
			count = list_count(object->grp_jobs_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_jobs_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->grp_mem_list)
			count = list_count(object->grp_mem_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_mem_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->grp_nodes_list)
			count = list_count(object->grp_nodes_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_nodes_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->grp_submit_jobs_list)
			count = list_count(object->grp_submit_jobs_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(
				object->grp_submit_jobs_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->grp_wall_list)
			count = list_count(object->grp_wall_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->grp_wall_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->id_list)
			count = list_count(object->id_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;

		if (object->max_cpu_mins_pj_list)
			count = list_count(object->max_cpu_mins_pj_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(
				object->max_cpu_mins_pj_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->max_cpu_run_mins_list)
			count = list_count(object->max_cpu_run_mins_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(
				object->max_cpu_run_mins_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->max_cpus_pj_list)
			count = list_count(object->max_cpus_pj_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->max_cpus_pj_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		if (object->max_jobs_list)
			count = list_count(object->max_jobs_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->max_jobs_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		if (object->max_nodes_pj_list)
			count = list_count(object->max_nodes_pj_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->max_nodes_pj_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		if (object->max_submit_jobs_list)
			count = list_count(object->max_submit_jobs_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(
				object->max_submit_jobs_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;
		if (object->max_wall_pj_list)
			count = list_count(object->max_wall_pj_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->max_wall_pj_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->only_defs, buffer);

		if (object->partition_list)
			count = list_count(object->partition_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->partition_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->parent_acct_list)
			count = list_count(object->parent_acct_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->parent_acct_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->qos_list)
			count = list_count(object->qos_list);

		pack32(count, buffer);

		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->qos_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack_time(object->usage_end, buffer);
		pack_time(object->usage_start, buffer);

		if (object->user_list)
			count = list_count(object->user_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->user_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->with_usage, buffer);
		pack16(object->with_deleted, buffer);
		pack16(object->with_raw_qos, buffer);
		pack16(object->with_sub_accts, buffer);
		pack16(object->without_parent_info, buffer);
		pack16(object->without_parent_limits, buffer);
	}
}

extern int slurmdb_unpack_association_cond(void **object,
					   uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_association_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_association_cond_t));
	char *tmp_info = NULL;
	*object = object_ptr;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->acct_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->def_qos_id_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->def_qos_id_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->fairshare_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->fairshare_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->grp_cpu_mins_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_cpu_mins_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->grp_cpu_run_mins_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_cpu_run_mins_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->grp_cpus_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_cpus_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->grp_jobs_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_jobs_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->grp_mem_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_mem_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->grp_nodes_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_nodes_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->grp_submit_jobs_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_submit_jobs_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->grp_wall_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->grp_wall_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->max_cpu_mins_pj_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_cpu_mins_pj_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->max_cpu_run_mins_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_cpu_run_mins_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->max_cpus_pj_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_cpus_pj_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->max_jobs_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_jobs_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->max_nodes_pj_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_nodes_pj_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->max_submit_jobs_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_submit_jobs_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->max_wall_pj_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->max_wall_pj_list,
					    tmp_info);
			}
		}

		safe_unpack16(&object_ptr->only_defs, buffer);

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->partition_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->partition_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->parent_acct_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->parent_acct_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->qos_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->qos_list, tmp_info);
			}
		}

		safe_unpack_time(&object_ptr->usage_end, buffer);
		safe_unpack_time(&object_ptr->usage_start, buffer);

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->user_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->user_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_usage, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
		safe_unpack16(&object_ptr->with_raw_qos, buffer);
		safe_unpack16(&object_ptr->with_sub_accts, buffer);
		safe_unpack16(&object_ptr->without_parent_info, buffer);
		safe_unpack16(&object_ptr->without_parent_limits, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_association_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_event_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	char *tmp_info = NULL;
	uint32_t count = NO_VAL;

	ListIterator itr = NULL;
	slurmdb_event_cond_t *object = (slurmdb_event_cond_t *)in;

	if (!object) {
		pack32(NO_VAL, buffer);
		pack32(NO_VAL, buffer);
		pack32(NO_VAL, buffer);
		pack16(0, buffer);
		packnull(buffer);
		pack_time(0, buffer);
		pack_time(0, buffer);
		pack32(NO_VAL, buffer);
		pack32(NO_VAL, buffer);
		pack32(NO_VAL, buffer);
		return;
	}

	if (object->cluster_list)
		count = list_count(object->cluster_list);

	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->cluster_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	pack32(object->cpus_max, buffer);
	pack32(object->cpus_min, buffer);
	pack16(object->event_type, buffer);

	if (object->node_list)
		count = list_count(object->node_list);

	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->node_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	pack_time(object->period_end, buffer);
	pack_time(object->period_start, buffer);

	if (object->reason_list)
		count = list_count(object->reason_list);

	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->reason_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	if (object->reason_uid_list)
		count = list_count(object->reason_uid_list);

	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->reason_uid_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	if (object->state_list)
		count = list_count(object->state_list);

	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->state_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
}

extern int slurmdb_unpack_event_cond(void **object, uint16_t rpc_version,
				     Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_event_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_event_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->cluster_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
					       buffer);
			list_append(object_ptr->cluster_list, tmp_info);
		}
	}
	safe_unpack32(&object_ptr->cpus_max, buffer);
	safe_unpack32(&object_ptr->cpus_min, buffer);
	safe_unpack16(&object_ptr->event_type, buffer);

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->node_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
					       buffer);
			list_append(object_ptr->node_list, tmp_info);
		}
	}

	safe_unpack_time(&object_ptr->period_end, buffer);
	safe_unpack_time(&object_ptr->period_start, buffer);

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->reason_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
					       buffer);
			list_append(object_ptr->reason_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->reason_uid_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
					       buffer);
			list_append(object_ptr->reason_uid_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->state_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
					       buffer);
			list_append(object_ptr->state_list, tmp_info);
		}
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_event_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_job_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	char *tmp_info = NULL;
	slurmdb_selected_step_t *job = NULL;
	uint32_t count = NO_VAL;

	ListIterator itr = NULL;
	slurmdb_job_cond_t *object = (slurmdb_job_cond_t *)in;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);	/* count(acct_list) */
			pack32(NO_VAL, buffer);	/* count(associd_list) */
			pack32(NO_VAL, buffer);	/* count(cluster_list) */
			pack32(0, buffer);	/* cpus_max */
			pack32(0, buffer);	/* cpus_min */
			pack16(0, buffer);	/* duplicates */
			pack32(0, buffer);	/* exitcode */
			pack32(NO_VAL, buffer);	/* count(groupid_list) */
			pack32(NO_VAL, buffer);	/* count(jobname_list) */
			pack32(0, buffer);	/* nodes_max */
			pack32(0, buffer);	/* nodes_min */
			pack32(NO_VAL, buffer);	/* count(partition_list) */
			pack32(NO_VAL, buffer);	/* count(qos_list) */
			pack32(NO_VAL, buffer);	/* count(resv_list) */
			pack32(NO_VAL, buffer);	/* count(resvid_list) */
			pack32(NO_VAL, buffer);	/* count(step_list) */
			pack32(NO_VAL, buffer);	/* count(state_list) */
			pack32(0, buffer);	/* timelimit_max */
			pack32(0, buffer);	/* timelimit_min */
			pack_time(0, buffer);	/* usage_end */
			pack_time(0, buffer);	/* usage_start */
			packnull(buffer);	/* used_nodes */
			pack32(NO_VAL, buffer);	/* count(userid_list) */
			pack32(NO_VAL, buffer);	/* count(wckey_list) */
			pack16(0, buffer);	/* without_steps */
			pack16(0, buffer);	/* without_usage_truncation */
			return;
		}

		if (object->acct_list)
			count = list_count(object->acct_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->acct_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->associd_list)
			count = list_count(object->associd_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->associd_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}

		if (object->cluster_list)
			count = list_count(object->cluster_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack32(object->cpus_max, buffer);
		pack32(object->cpus_min, buffer);
		pack16(object->duplicates, buffer);
		pack32((uint32_t)object->exitcode, buffer);

		if (object->groupid_list)
			count = list_count(object->groupid_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->groupid_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->jobname_list)
			count = list_count(object->jobname_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->jobname_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack32(object->nodes_max, buffer);
		pack32(object->nodes_min, buffer);

		if (object->partition_list)
			count = list_count(object->partition_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->partition_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->qos_list)
			count = list_count(object->qos_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->qos_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->resv_list)
			count = list_count(object->resv_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->resv_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->resvid_list)
			count = list_count(object->resvid_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->resvid_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->step_list)
			count = list_count(object->step_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->step_list);
			while ((job = list_next(itr))) {
				slurmdb_pack_selected_step(job, rpc_version,
							   buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->state_list)
			count = list_count(object->state_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->state_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack32(object->timelimit_max, buffer);
		pack32(object->timelimit_min, buffer);
		pack_time(object->usage_end, buffer);
		pack_time(object->usage_start, buffer);

		packstr(object->used_nodes, buffer);

		if (object->userid_list)
			count = list_count(object->userid_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->userid_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->wckey_list)
			count = list_count(object->wckey_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->wckey_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->without_steps, buffer);
		pack16(object->without_usage_truncation, buffer);
	}
}

extern int slurmdb_unpack_job_cond(void **object, uint16_t rpc_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_job_cond_t *object_ptr = xmalloc(sizeof(slurmdb_job_cond_t));
	char *tmp_info = NULL;
	slurmdb_selected_step_t *job = NULL;

	*object = object_ptr;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->acct_list = list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->associd_list =
				list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->associd_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack32(&object_ptr->cpus_max, buffer);
		safe_unpack32(&object_ptr->cpus_min, buffer);
		safe_unpack16(&object_ptr->duplicates, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		object_ptr->exitcode = (int32_t)uint32_tmp;

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->groupid_list =
				list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->groupid_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->jobname_list =
				list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->jobname_list, tmp_info);
			}
		}

		safe_unpack32(&object_ptr->nodes_max, buffer);
		safe_unpack32(&object_ptr->nodes_min, buffer);

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->partition_list =
				list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->partition_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->qos_list =
				list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->qos_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->resv_list =
				list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->resv_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->resvid_list =
				list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->resvid_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->step_list =
				list_create(slurmdb_destroy_selected_step);
			for (i=0; i<count; i++) {
				slurmdb_unpack_selected_step(
					&job, rpc_version, buffer);
				list_append(object_ptr->step_list, job);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->state_list =
				list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->state_list, tmp_info);
			}
		}

		safe_unpack32(&object_ptr->timelimit_max, buffer);
		safe_unpack32(&object_ptr->timelimit_min, buffer);
		safe_unpack_time(&object_ptr->usage_end, buffer);
		safe_unpack_time(&object_ptr->usage_start, buffer);

		safe_unpackstr_xmalloc(&object_ptr->used_nodes,
				       &uint32_tmp, buffer);

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->userid_list =
				list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->userid_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->wckey_list =
				list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->wckey_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->without_steps, buffer);
		safe_unpack16(&object_ptr->without_usage_truncation, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_job_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_job_modify_cond(void *in, uint16_t rpc_version,
					 Buf buffer)
{
	slurmdb_job_modify_cond_t *cond = (slurmdb_job_modify_cond_t *)in;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!cond) {
			packnull(buffer);
			pack32(NO_VAL, buffer);
			return;
		}
		packstr(cond->cluster, buffer);
		pack32(cond->job_id, buffer);
	}
}

extern int slurmdb_unpack_job_modify_cond(void **object, uint16_t rpc_version,
					  Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_job_modify_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_job_modify_cond_t));

	*object = object_ptr;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp,
				       buffer);
		safe_unpack32(&object_ptr->job_id, buffer);
	}
	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_job_modify_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_job_rec(void *object, uint16_t rpc_version, Buf buffer)
{
	slurmdb_job_rec_t *job = (slurmdb_job_rec_t *)object;
	ListIterator itr = NULL;
	slurmdb_step_rec_t *step = NULL;
	uint32_t count = 0;

	if (rpc_version >= SLURM_14_11_PROTOCOL_VERSION) {
		packstr(job->account, buffer);
		pack32(job->alloc_cpus, buffer);
		packstr(job->alloc_gres, buffer);
		pack32(job->alloc_nodes, buffer);
		pack32(job->array_job_id, buffer);
		pack32(job->array_max_tasks, buffer);
		pack32(job->array_task_id, buffer);
		packstr(job->array_task_str, buffer);
		pack32(job->associd, buffer);
		packstr(job->blockid, buffer);
		packstr(job->cluster, buffer);
		pack32((uint32_t)job->derived_ec, buffer);
		packstr(job->derived_es, buffer);
		pack32(job->elapsed, buffer);
		pack_time(job->eligible, buffer);
		pack_time(job->end, buffer);
		pack32((uint32_t)job->exitcode, buffer);
		/* the first_step_ptr
		   is set up on the client side so does
		   not need to be packed */
		pack32(job->gid, buffer);
		pack32(job->jobid, buffer);
		packstr(job->jobname, buffer);
		pack32(job->lft, buffer);
		packstr(job->nodes, buffer);
		packstr(job->partition, buffer);
		pack32(job->priority, buffer);
		pack32(job->qosid, buffer);
		pack32(job->req_cpus, buffer);
		packstr(job->req_gres, buffer);
		pack32(job->req_mem, buffer);
		pack32(job->requid, buffer);
		packstr(job->resv_name, buffer);
		pack32(job->resvid, buffer);
		pack32(job->show_full, buffer);
		pack_time(job->start, buffer);
		pack16((uint16_t)job->state, buffer);
		_pack_slurmdb_stats(&job->stats, rpc_version, buffer);
		if (job->steps)
			count = list_count(job->steps);
		pack32(count, buffer);
		if (count) {
			itr = list_iterator_create(job->steps);
			while ((step = list_next(itr))) {
				slurmdb_pack_step_rec(step, rpc_version,
						      buffer);
			}
			list_iterator_destroy(itr);
		}
		pack_time(job->submit, buffer);
		pack32(job->suspended, buffer);
		pack32(job->sys_cpu_sec, buffer);
		pack32(job->sys_cpu_usec, buffer);
		pack32(job->timelimit, buffer);
		pack32(job->tot_cpu_sec, buffer);
		pack32(job->tot_cpu_usec, buffer);
		pack16(job->track_steps, buffer);
		pack32(job->uid, buffer);
		packstr(job->user, buffer);
		pack32(job->user_cpu_sec, buffer);
		pack32(job->user_cpu_usec, buffer);
		packstr(job->wckey, buffer); /* added for rpc_version 4 */
		pack32(job->wckeyid, buffer); /* added for rpc_version 4 */
	} else if (rpc_version >= SLURMDBD_MIN_VERSION) {
		packstr(job->account, buffer);
		pack32(job->alloc_cpus, buffer);
		pack32(job->alloc_nodes, buffer);
		pack32(job->associd, buffer);
		packstr(job->blockid, buffer);
		packstr(job->cluster, buffer);
		pack32((uint32_t)job->derived_ec, buffer);
		packstr(job->derived_es, buffer);
		pack32(job->elapsed, buffer);
		pack_time(job->eligible, buffer);
		pack_time(job->end, buffer);
		pack32((uint32_t)job->exitcode, buffer);
		/* the first_step_ptr
		   is set up on the client side so does
		   not need to be packed */
		pack32(job->gid, buffer);
		pack32(job->jobid, buffer);
		packstr(job->jobname, buffer);
		pack32(job->lft, buffer);
		packstr(job->nodes, buffer);
		packstr(job->partition, buffer);
		pack32(job->priority, buffer);
		pack32(job->qosid, buffer);
		pack32(job->req_cpus, buffer);
		pack32(job->req_mem, buffer);
		pack32(job->requid, buffer);
		pack32(job->resvid, buffer);
		pack32(job->show_full, buffer);
		pack_time(job->start, buffer);
		pack16((uint16_t)job->state, buffer);
		_pack_slurmdb_stats(&job->stats, rpc_version, buffer);
		if (job->steps)
			count = list_count(job->steps);
		pack32(count, buffer);
		if (count) {
			itr = list_iterator_create(job->steps);
			while ((step = list_next(itr))) {
				slurmdb_pack_step_rec(step, rpc_version,
						      buffer);
			}
			list_iterator_destroy(itr);
		}
		pack_time(job->submit, buffer);
		pack32(job->suspended, buffer);
		pack32(job->sys_cpu_sec, buffer);
		pack32(job->sys_cpu_usec, buffer);
		pack32(job->timelimit, buffer);
		pack32(job->tot_cpu_sec, buffer);
		pack32(job->tot_cpu_usec, buffer);
		pack16(job->track_steps, buffer);
		pack32(job->uid, buffer);
		packstr(job->user, buffer);
		pack32(job->user_cpu_sec, buffer);
		pack32(job->user_cpu_usec, buffer);
		packstr(job->wckey, buffer); /* added for rpc_version 4 */
		pack32(job->wckeyid, buffer); /* added for rpc_version 4 */
	}
}

extern int slurmdb_unpack_job_rec(void **job, uint16_t rpc_version, Buf buffer)
{
	slurmdb_job_rec_t *job_ptr = xmalloc(sizeof(slurmdb_job_rec_t));
	int i = 0;
	slurmdb_step_rec_t *step = NULL;
	uint32_t count = 0;
	uint32_t uint32_tmp;
	uint16_t uint16_tmp;

	*job = job_ptr;

	job_ptr->array_job_id = 0;
	job_ptr->array_task_id = NO_VAL;

	if (rpc_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&job_ptr->account, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->alloc_cpus, buffer);
		safe_unpackstr_xmalloc(&job_ptr->alloc_gres, &uint32_tmp,
				       buffer);
		safe_unpack32(&job_ptr->alloc_nodes, buffer);
		safe_unpack32(&job_ptr->array_job_id, buffer);
		safe_unpack32(&job_ptr->array_max_tasks, buffer);
		safe_unpack32(&job_ptr->array_task_id, buffer);
		safe_unpackstr_xmalloc(&job_ptr->array_task_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->associd, buffer);
		safe_unpackstr_xmalloc(&job_ptr->blockid, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_ptr->cluster, &uint32_tmp, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		job_ptr->derived_ec = (int32_t)uint32_tmp;
		safe_unpackstr_xmalloc(&job_ptr->derived_es, &uint32_tmp,
				       buffer);
		safe_unpack32(&job_ptr->elapsed, buffer);
		safe_unpack_time(&job_ptr->eligible, buffer);
		safe_unpack_time(&job_ptr->end, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		job_ptr->exitcode = (int32_t)uint32_tmp;
		safe_unpack32(&job_ptr->gid, buffer);
		safe_unpack32(&job_ptr->jobid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->jobname, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->lft, buffer);
		safe_unpackstr_xmalloc(&job_ptr->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_ptr->partition, &uint32_tmp,
				       buffer);
		safe_unpack32(&job_ptr->priority, buffer);
		safe_unpack32(&job_ptr->qosid, buffer);
		safe_unpack32(&job_ptr->req_cpus, buffer);
		safe_unpackstr_xmalloc(&job_ptr->req_gres, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->req_mem, buffer);
		safe_unpack32(&job_ptr->requid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->resv_name, &uint32_tmp,
				       buffer);
		safe_unpack32(&job_ptr->resvid, buffer);
		safe_unpack32(&job_ptr->show_full, buffer);
		safe_unpack_time(&job_ptr->start, buffer);
		safe_unpack16(&uint16_tmp, buffer);
		job_ptr->state = uint16_tmp;
		if (_unpack_slurmdb_stats(&job_ptr->stats, rpc_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack32(&count, buffer);
		job_ptr->steps = list_create(slurmdb_destroy_step_rec);
		for(i=0; i<count; i++) {
			if (slurmdb_unpack_step_rec(&step, rpc_version, buffer)
			    == SLURM_ERROR)
				goto unpack_error;

			step->job_ptr = job_ptr;
			if (!job_ptr->first_step_ptr)
				job_ptr->first_step_ptr = step;
			list_append(job_ptr->steps, step);
		}

		safe_unpack_time(&job_ptr->submit, buffer);
		safe_unpack32(&job_ptr->suspended, buffer);
		safe_unpack32(&job_ptr->sys_cpu_sec, buffer);
		safe_unpack32(&job_ptr->sys_cpu_usec, buffer);
		safe_unpack32(&job_ptr->timelimit, buffer);
		safe_unpack32(&job_ptr->tot_cpu_sec, buffer);
		safe_unpack32(&job_ptr->tot_cpu_usec, buffer);
		safe_unpack16(&job_ptr->track_steps, buffer);
		safe_unpack32(&job_ptr->uid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->user, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->user_cpu_sec, buffer);
		safe_unpack32(&job_ptr->user_cpu_usec, buffer);
		safe_unpackstr_xmalloc(&job_ptr->wckey, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->wckeyid, buffer);
	} else if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpackstr_xmalloc(&job_ptr->account, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->alloc_cpus, buffer);
		safe_unpack32(&job_ptr->alloc_nodes, buffer);
		safe_unpack32(&job_ptr->associd, buffer);
		safe_unpackstr_xmalloc(&job_ptr->blockid, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_ptr->cluster, &uint32_tmp, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		job_ptr->derived_ec = (int32_t)uint32_tmp;
		safe_unpackstr_xmalloc(&job_ptr->derived_es, &uint32_tmp,
				       buffer);
		safe_unpack32(&job_ptr->elapsed, buffer);
		safe_unpack_time(&job_ptr->eligible, buffer);
		safe_unpack_time(&job_ptr->end, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		job_ptr->exitcode = (int32_t)uint32_tmp;
		safe_unpack32(&job_ptr->gid, buffer);
		safe_unpack32(&job_ptr->jobid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->jobname, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->lft, buffer);
		safe_unpackstr_xmalloc(&job_ptr->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_ptr->partition, &uint32_tmp,
				       buffer);
		safe_unpack32(&job_ptr->priority, buffer);
		safe_unpack32(&job_ptr->qosid, buffer);
		safe_unpack32(&job_ptr->req_cpus, buffer);
		safe_unpack32(&job_ptr->req_mem, buffer);
		safe_unpack32(&job_ptr->requid, buffer);
		safe_unpack32(&job_ptr->resvid, buffer);
		safe_unpack32(&job_ptr->show_full, buffer);
		safe_unpack_time(&job_ptr->start, buffer);
		safe_unpack16(&uint16_tmp, buffer);
		job_ptr->state = uint16_tmp;
		if (_unpack_slurmdb_stats(&job_ptr->stats, rpc_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack32(&count, buffer);
		job_ptr->steps = list_create(slurmdb_destroy_step_rec);
		for(i=0; i<count; i++) {
			if (slurmdb_unpack_step_rec(&step, rpc_version, buffer)
			    == SLURM_ERROR)
				goto unpack_error;

			step->job_ptr = job_ptr;
			if (!job_ptr->first_step_ptr)
				job_ptr->first_step_ptr = step;
			list_append(job_ptr->steps, step);
		}

		safe_unpack_time(&job_ptr->submit, buffer);
		safe_unpack32(&job_ptr->suspended, buffer);
		safe_unpack32(&job_ptr->sys_cpu_sec, buffer);
		safe_unpack32(&job_ptr->sys_cpu_usec, buffer);
		safe_unpack32(&job_ptr->timelimit, buffer);
		safe_unpack32(&job_ptr->tot_cpu_sec, buffer);
		safe_unpack32(&job_ptr->tot_cpu_usec, buffer);
		safe_unpack16(&job_ptr->track_steps, buffer);
		safe_unpack32(&job_ptr->uid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->user, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->user_cpu_sec, buffer);
		safe_unpack32(&job_ptr->user_cpu_usec, buffer);
		safe_unpackstr_xmalloc(&job_ptr->wckey, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->wckeyid, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_job_rec(job_ptr);
	*job = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_qos_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	slurmdb_qos_cond_t *object = (slurmdb_qos_cond_t *)in;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}

		if (object->description_list)
			count = list_count(object->description_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->description_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->id_list)
			count = list_count(object->id_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->name_list)
			count = list_count(object->name_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->name_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->preempt_mode, buffer);
		pack16(object->with_deleted, buffer);
	}
}

extern int slurmdb_unpack_qos_cond(void **object, uint16_t rpc_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_qos_cond_t *object_ptr = xmalloc(sizeof(slurmdb_qos_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->description_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->description_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->name_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->preempt_mode, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_qos_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_reservation_cond(void *in, uint16_t rpc_version,
					  Buf buffer)
{
	slurmdb_reservation_cond_t *object = (slurmdb_reservation_cond_t *)in;
	uint32_t count = NO_VAL;
	ListIterator itr = NULL;
	char *tmp_info = NULL;

	if (!object) {
		pack32((uint32_t)NO_VAL, buffer);
		pack16(0, buffer);
		pack32((uint16_t)NO_VAL, buffer);
		pack32((uint16_t)NO_VAL, buffer);
		packnull(buffer);
		pack_time(0, buffer);
		pack_time(0, buffer);
		pack16(0, buffer);
		return;
	}

	if (object->cluster_list)
		count = list_count(object->cluster_list);

	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->cluster_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	pack16(object->flags, buffer);

	if (object->id_list)
		count = list_count(object->id_list);

	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->id_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	if (object->name_list)
		count = list_count(object->name_list);

	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->name_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}

	packstr(object->nodes, buffer);
	pack_time(object->time_end, buffer);
	pack_time(object->time_start, buffer);
	pack16(object->with_usage, buffer);
}

extern int slurmdb_unpack_reservation_cond(void **object, uint16_t rpc_version,
					   Buf buffer)
{
	uint32_t uint32_tmp, count;
	int i = 0;
	char *tmp_info = NULL;
	slurmdb_reservation_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_reservation_cond_t));

	*object = object_ptr;

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->cluster_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->cluster_list, tmp_info);
		}
	}

	safe_unpack16(&object_ptr->flags, buffer);

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->id_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->id_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->name_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp, buffer);
			list_append(object_ptr->name_list, tmp_info);
		}
	}

	safe_unpackstr_xmalloc(&object_ptr->nodes, &uint32_tmp, buffer);
	safe_unpack_time(&object_ptr->time_end, buffer);
	safe_unpack_time(&object_ptr->time_start, buffer);
	safe_unpack16(&object_ptr->with_usage, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_reservation_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_selected_step(slurmdb_selected_step_t *step,
				       uint16_t rpc_version, Buf buffer)
{
	if (rpc_version >= SLURM_14_11_PROTOCOL_VERSION) {
		pack32(step->array_task_id, buffer);
		pack32(step->jobid, buffer);
		pack32(step->stepid, buffer);
	} else if (rpc_version >= SLURMDBD_MIN_VERSION) {
		pack32(step->jobid, buffer);
		pack32(step->stepid, buffer);
	}
}

extern int slurmdb_unpack_selected_step(slurmdb_selected_step_t **step,
					uint16_t rpc_version, Buf buffer)
{
	slurmdb_selected_step_t *step_ptr =
		xmalloc(sizeof(slurmdb_selected_step_t));

	*step = step_ptr;

	step_ptr->array_task_id = NO_VAL;

	if (rpc_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpack32(&step_ptr->array_task_id, buffer);
		safe_unpack32(&step_ptr->jobid, buffer);
		safe_unpack32(&step_ptr->stepid, buffer);
	} else if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack32(&step_ptr->jobid, buffer);
		safe_unpack32(&step_ptr->stepid, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_selected_step(step_ptr);
	*step = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_step_rec(slurmdb_step_rec_t *step,
				  uint16_t rpc_version, Buf buffer)
{
	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		pack32(step->elapsed, buffer);
		pack_time(step->end, buffer);
		pack32((uint32_t)step->exitcode, buffer);
		/* the job_ptr is set up on the client side so does
		 * not need to be packed */
		pack32(step->ncpus, buffer);
		pack32(step->nnodes, buffer);
		packstr(step->nodes, buffer);
		pack32(step->ntasks, buffer);
		pack32(step->req_cpufreq, buffer);
		pack32(step->requid, buffer);
		_pack_slurmdb_stats(&step->stats, rpc_version, buffer);
		pack_time(step->start, buffer);
		pack16(step->state, buffer);
		pack32(step->stepid, buffer);	/* job's step number */
		packstr(step->stepname, buffer);
		pack32(step->suspended, buffer);
		pack32(step->sys_cpu_sec, buffer);
		pack32(step->sys_cpu_usec, buffer);
		pack16(step->task_dist, buffer);
		pack32(step->tot_cpu_sec, buffer);
		pack32(step->tot_cpu_usec, buffer);
		pack32(step->user_cpu_sec, buffer);
		pack32(step->user_cpu_usec, buffer);
	}
}

extern int slurmdb_unpack_step_rec(slurmdb_step_rec_t **step,
				   uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	uint16_t uint16_tmp;
	slurmdb_step_rec_t *step_ptr = xmalloc(sizeof(slurmdb_step_rec_t));

	*step = step_ptr;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack32(&step_ptr->elapsed, buffer);
		safe_unpack_time(&step_ptr->end, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		step_ptr->exitcode = (int32_t)uint32_tmp;
		safe_unpack32(&step_ptr->ncpus, buffer);
		safe_unpack32(&step_ptr->nnodes, buffer);
		safe_unpackstr_xmalloc(&step_ptr->nodes, &uint32_tmp, buffer);
		safe_unpack32(&step_ptr->ntasks, buffer);
		safe_unpack32(&step_ptr->req_cpufreq, buffer);
		safe_unpack32(&step_ptr->requid, buffer);
		if (_unpack_slurmdb_stats(&step_ptr->stats, rpc_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack_time(&step_ptr->start, buffer);
		safe_unpack16(&uint16_tmp, buffer);
		step_ptr->state = uint16_tmp;
		safe_unpack32(&step_ptr->stepid, buffer);
		safe_unpackstr_xmalloc(&step_ptr->stepname,
				       &uint32_tmp, buffer);
		safe_unpack32(&step_ptr->suspended, buffer);
		safe_unpack32(&step_ptr->sys_cpu_sec, buffer);
		safe_unpack32(&step_ptr->sys_cpu_usec, buffer);
		safe_unpack16(&step_ptr->task_dist, buffer);
		safe_unpack32(&step_ptr->tot_cpu_sec, buffer);
		safe_unpack32(&step_ptr->tot_cpu_usec, buffer);
		safe_unpack32(&step_ptr->user_cpu_sec, buffer);
		safe_unpack32(&step_ptr->user_cpu_usec, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_step_rec(step_ptr);
	*step = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_res_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	slurmdb_res_cond_t *object = (slurmdb_res_cond_t *)in;
	uint32_t count = NO_VAL;

	if (!object) {
		pack32(NO_VAL, buffer);
		pack32(NO_VAL, buffer);
		pack32(SLURMDB_RES_FLAG_NOTSET, buffer);
		pack32(NO_VAL, buffer);
		pack32(NO_VAL, buffer);
		pack32(NO_VAL, buffer);
		pack32(NO_VAL, buffer);
		pack32(NO_VAL, buffer);
		pack32(NO_VAL, buffer);
		pack16(0, buffer);
		pack16(0, buffer);

		return;
	}
	if (object->cluster_list)
		count = list_count(object->cluster_list);

	pack32(count, buffer);

	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->cluster_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	if (object->description_list)
		count = list_count(object->description_list);

	pack32(count, buffer);

	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->description_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	pack32(object->flags, buffer);

	if (object->id_list)
		count = list_count(object->id_list);

	pack32(count, buffer);

	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->id_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	if (object->manager_list)
		count = list_count(object->manager_list);

	pack32(count, buffer);

	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->manager_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	if (object->name_list)
		count = list_count(object->name_list);

	pack32(count, buffer);

	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->name_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	if (object->percent_list)
		count = list_count(object->percent_list);

	pack32(count, buffer);

	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->percent_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	if (object->server_list)
		count = list_count(object->server_list);

	pack32(count, buffer);

	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->server_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	if (object->type_list)
		count = list_count(object->type_list);

	pack32(count, buffer);

	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->type_list);
		while ((tmp_info = list_next(itr))) {
			packstr(tmp_info, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = NO_VAL;

	pack16(object->with_deleted, buffer);
	pack16(object->with_clusters, buffer);
}

extern int slurmdb_unpack_res_cond(void **object, uint16_t rpc_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_res_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_res_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	slurmdb_init_res_cond(object_ptr, 0);

	safe_unpack32(&count, buffer);
	if (count && count != NO_VAL) {
		object_ptr->cluster_list =
			list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info,
					       &uint32_tmp, buffer);
			list_append(object_ptr->cluster_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if (count && count != NO_VAL) {
		object_ptr->description_list =
			list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info,
					       &uint32_tmp, buffer);
			list_append(object_ptr->description_list, tmp_info);
		}
	}

	safe_unpack32(&object_ptr->flags, buffer);

	safe_unpack32(&count, buffer);
	if (count && count != NO_VAL) {
		object_ptr->id_list =
			list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info,
					       &uint32_tmp, buffer);
			list_append(object_ptr->id_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if (count && count != NO_VAL) {
		object_ptr->manager_list =
			list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info,
					       &uint32_tmp, buffer);
			list_append(object_ptr->manager_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if (count && count != NO_VAL) {
		object_ptr->name_list =
			list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info,
					       &uint32_tmp, buffer);
			list_append(object_ptr->name_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if (count && count != NO_VAL) {
		object_ptr->percent_list =
			list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info,
					       &uint32_tmp, buffer);
			list_append(object_ptr->percent_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if (count && count != NO_VAL) {
		object_ptr->server_list =
			list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info,
					       &uint32_tmp, buffer);
			list_append(object_ptr->server_list, tmp_info);
		}
	}

	safe_unpack32(&count, buffer);
	if (count && count != NO_VAL) {
		object_ptr->type_list =
			list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&tmp_info,
					       &uint32_tmp, buffer);
			list_append(object_ptr->type_list, tmp_info);
		}
	}

	safe_unpack16(&object_ptr->with_deleted, buffer);
	safe_unpack16(&object_ptr->with_clusters, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_res_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_txn_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	slurmdb_txn_cond_t *object = (slurmdb_txn_cond_t *)in;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			return;
		}
		if (object->acct_list)
			count = list_count(object->acct_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->acct_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->action_list)
			count = list_count(object->action_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->action_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->actor_list)
			count = list_count(object->actor_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->actor_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->cluster_list)
			count = list_count(object->cluster_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->id_list)
			count = list_count(object->id_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->info_list)
			count = list_count(object->info_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->info_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->name_list)
			count = list_count(object->name_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->name_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack_time(object->time_end, buffer);
		pack_time(object->time_start, buffer);
		if (object->user_list)
			count = list_count(object->user_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->user_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->with_assoc_info, buffer);
	}
}

extern int slurmdb_unpack_txn_cond(void **object, uint16_t rpc_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_txn_cond_t *object_ptr = xmalloc(sizeof(slurmdb_txn_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;
	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->acct_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->action_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->action_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->actor_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->actor_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->info_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->info_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->name_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpack_time(&object_ptr->time_end, buffer);
		safe_unpack_time(&object_ptr->time_start, buffer);

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->user_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->user_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_assoc_info, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_txn_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_wckey_cond(void *in, uint16_t rpc_version, Buf buffer)
{
	char *tmp_info = NULL;
	uint32_t count = NO_VAL;

	ListIterator itr = NULL;
	slurmdb_wckey_cond_t *object = (slurmdb_wckey_cond_t *)in;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack16(0, buffer);

			pack_time(0, buffer);
			pack_time(0, buffer);

			pack32(NO_VAL, buffer);

			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}

		if (object->cluster_list)
			count = list_count(object->cluster_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->id_list)
			count = list_count(object->id_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;

		if (object->name_list)
			count = list_count(object->name_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->name_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16(object->only_defs, buffer);

		pack_time(object->usage_end, buffer);
		pack_time(object->usage_start, buffer);

		if (object->user_list)
			count = list_count(object->user_list);

		pack32(count, buffer);
		if (count && count != NO_VAL) {
			itr = list_iterator_create(object->user_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->with_usage, buffer);
		pack16(object->with_deleted, buffer);
	}
}

extern int slurmdb_unpack_wckey_cond(void **object, uint16_t rpc_version,
				     Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_wckey_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_wckey_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->name_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->only_defs, buffer);

		safe_unpack_time(&object_ptr->usage_end, buffer);
		safe_unpack_time(&object_ptr->usage_start, buffer);

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->user_list =
				list_create(slurm_destroy_char);
			for(i=0; i<count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->user_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_usage, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_wckey_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_archive_cond(void *in, uint16_t rpc_version,
				      Buf buffer)
{
	slurmdb_archive_cond_t *object = (slurmdb_archive_cond_t *)in;

	if (rpc_version >= SLURM_14_11_PROTOCOL_VERSION) {
		if (!object) {
			packnull(buffer);
			packnull(buffer);
			slurmdb_pack_job_cond(NULL, rpc_version, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			return;
		}

		packstr(object->archive_dir, buffer);
		packstr(object->archive_script, buffer);
		slurmdb_pack_job_cond(object->job_cond, rpc_version, buffer);
		pack32(object->purge_event, buffer);
		pack32(object->purge_job, buffer);
		pack32(object->purge_resv, buffer);
		pack32(object->purge_step, buffer);
		pack32(object->purge_suspend, buffer);
	} else if (rpc_version >= SLURMDBD_MIN_VERSION) {
		if (!object) {
			packnull(buffer);
			packnull(buffer);
			slurmdb_pack_job_cond(NULL, rpc_version, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			return;
		}

		packstr(object->archive_dir, buffer);
		packstr(object->archive_script, buffer);
		slurmdb_pack_job_cond(object->job_cond, rpc_version, buffer);
		pack32(object->purge_event, buffer);
		pack32(object->purge_job, buffer);
		pack32(object->purge_step, buffer);
		pack32(object->purge_suspend, buffer);
	}
}

extern int slurmdb_unpack_archive_cond(void **object, uint16_t rpc_version,
				       Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_archive_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_archive_cond_t));

	*object = object_ptr;

	if (rpc_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object_ptr->archive_dir,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->archive_script,
				       &uint32_tmp, buffer);
		if (slurmdb_unpack_job_cond((void *)&object_ptr->job_cond,
					    rpc_version, buffer) == SLURM_ERROR)
			goto unpack_error;
		safe_unpack32(&object_ptr->purge_event, buffer);
		safe_unpack32(&object_ptr->purge_job, buffer);
		safe_unpack32(&object_ptr->purge_resv, buffer);
		safe_unpack32(&object_ptr->purge_step, buffer);
		safe_unpack32(&object_ptr->purge_suspend, buffer);
	} else if (rpc_version >= SLURMDBD_MIN_VERSION) {
		safe_unpackstr_xmalloc(&object_ptr->archive_dir,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->archive_script,
				       &uint32_tmp, buffer);
		if (slurmdb_unpack_job_cond((void *)&object_ptr->job_cond,
					    rpc_version, buffer) == SLURM_ERROR)
			goto unpack_error;
		safe_unpack32(&object_ptr->purge_event, buffer);
		safe_unpack32(&object_ptr->purge_job, buffer);
		safe_unpack32(&object_ptr->purge_step, buffer);
		safe_unpack32(&object_ptr->purge_suspend, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_archive_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;

}

extern void slurmdb_pack_update_object(slurmdb_update_object_t *object,
				       uint16_t rpc_version, Buf buffer)
{
	uint32_t count = NO_VAL;
	ListIterator itr = NULL;
	void *slurmdb_object = NULL;
	void (*my_function) (void *object, uint16_t rpc_version, Buf buffer);

	switch(object->type) {
	case SLURMDB_MODIFY_USER:
	case SLURMDB_ADD_USER:
	case SLURMDB_REMOVE_USER:
	case SLURMDB_ADD_COORD:
	case SLURMDB_REMOVE_COORD:
		my_function = slurmdb_pack_user_rec;
		break;
	case SLURMDB_ADD_ASSOC:
	case SLURMDB_MODIFY_ASSOC:
	case SLURMDB_REMOVE_ASSOC:
	case SLURMDB_REMOVE_ASSOC_USAGE:
		my_function = slurmdb_pack_association_rec;
		break;
	case SLURMDB_ADD_QOS:
	case SLURMDB_MODIFY_QOS:
	case SLURMDB_REMOVE_QOS:
	case SLURMDB_REMOVE_QOS_USAGE:
		my_function = slurmdb_pack_qos_rec;
		break;
	case SLURMDB_ADD_WCKEY:
	case SLURMDB_MODIFY_WCKEY:
	case SLURMDB_REMOVE_WCKEY:
		my_function = slurmdb_pack_wckey_rec;
		break;
	case SLURMDB_ADD_CLUSTER:
	case SLURMDB_REMOVE_CLUSTER:
		pack16(object->type, buffer);
		return;
	case SLURMDB_ADD_RES:
	case SLURMDB_MODIFY_RES:
	case SLURMDB_REMOVE_RES:
		my_function = slurmdb_pack_res_rec;
		break;
	case SLURMDB_UPDATE_NOTSET:
	default:
		error("pack: unknown type set in update_object: %d",
		      object->type);
		return;
	}

	pack16(object->type, buffer);
	if (object->objects)
		count = list_count(object->objects);

	pack32(count, buffer);
	if (count && count != NO_VAL) {
		itr = list_iterator_create(object->objects);
		while ((slurmdb_object = list_next(itr))) {
			(*(my_function))(slurmdb_object, rpc_version, buffer);
		}
		list_iterator_destroy(itr);
	}
}

extern int slurmdb_unpack_update_object(slurmdb_update_object_t **object,
					uint16_t rpc_version, Buf buffer)
{
	int i;
	uint32_t count;
	slurmdb_update_object_t *object_ptr =
		xmalloc(sizeof(slurmdb_update_object_t));
	void *slurmdb_object = NULL;
	int (*my_function) (void **object, uint16_t rpc_version, Buf buffer);
	void (*my_destroy) (void *object);

	*object = object_ptr;

	safe_unpack16(&object_ptr->type, buffer);
	switch(object_ptr->type) {
	case SLURMDB_MODIFY_USER:
	case SLURMDB_ADD_USER:
	case SLURMDB_REMOVE_USER:
	case SLURMDB_ADD_COORD:
	case SLURMDB_REMOVE_COORD:
		my_function = slurmdb_unpack_user_rec;
		my_destroy = slurmdb_destroy_user_rec;
		break;
	case SLURMDB_ADD_ASSOC:
	case SLURMDB_MODIFY_ASSOC:
	case SLURMDB_REMOVE_ASSOC:
	case SLURMDB_REMOVE_ASSOC_USAGE:
		my_function = slurmdb_unpack_association_rec;
		my_destroy = slurmdb_destroy_association_rec;
		break;
	case SLURMDB_ADD_QOS:
	case SLURMDB_MODIFY_QOS:
	case SLURMDB_REMOVE_QOS:
	case SLURMDB_REMOVE_QOS_USAGE:
		my_function = slurmdb_unpack_qos_rec;
		my_destroy = slurmdb_destroy_qos_rec;
		break;
	case SLURMDB_ADD_WCKEY:
	case SLURMDB_MODIFY_WCKEY:
	case SLURMDB_REMOVE_WCKEY:
		my_function = slurmdb_unpack_wckey_rec;
		my_destroy = slurmdb_destroy_wckey_rec;
		break;
	case SLURMDB_ADD_CLUSTER:
	case SLURMDB_REMOVE_CLUSTER:
		/* we don't pack anything on these */
		return SLURM_SUCCESS;
	case SLURMDB_ADD_RES:
	case SLURMDB_MODIFY_RES:
	case SLURMDB_REMOVE_RES:
		my_function = slurmdb_unpack_res_rec;
		my_destroy = slurmdb_destroy_res_rec;
		break;
	case SLURMDB_UPDATE_NOTSET:
	default:
		error("unpack: unknown type set in update_object: %d",
		      object_ptr->type);
		goto unpack_error;
	}
	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		object_ptr->objects = list_create((*(my_destroy)));
		for(i=0; i<count; i++) {
			if (((*(my_function))(&slurmdb_object,
					      rpc_version, buffer))
			    == SLURM_ERROR)
				goto unpack_error;
			list_append(object_ptr->objects, slurmdb_object);
		}
	}
	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_update_object(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

