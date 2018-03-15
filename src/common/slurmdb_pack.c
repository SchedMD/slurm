/*****************************************************************************\
 *  slurmdb_pack.h - un/pack definitions used by slurmdb api
 ******************************************************************************
 *  Copyright (C) 2011-2015 SchedMD LLC.
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@schedmd.com, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

static uint32_t _list_count_null(List l)
{
	uint32_t count = NO_VAL;

	if (l)
		count = list_count(l);
	return count;
}

static void _pack_slurmdb_stats(slurmdb_stats_t *stats,
				uint16_t protocol_version, Buf buffer)
{
	int i=0;

	xassert(buffer);

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (!stats) {
			for (i=0; i<4; i++)
				pack64(0, buffer);

			pack32(0, buffer);

			for (i=0; i<9; i++)
				packdouble(0, buffer);

			for (i=0; i<12; i++) {
				pack32(0, buffer);
			}
			return;
		}

		pack64(stats->vsize_max, buffer);
		pack64(stats->rss_max, buffer);
		pack64(stats->pages_max, buffer);
		pack64(stats->consumed_energy, buffer);

		pack32(stats->cpu_min, buffer);

		packdouble(stats->vsize_ave, buffer);
		packdouble(stats->rss_ave, buffer);
		packdouble(stats->pages_ave, buffer);
		packdouble(stats->cpu_ave, buffer);
		packdouble(stats->act_cpufreq, buffer);
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
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
		packdouble((double)stats->consumed_energy, buffer);
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
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

static int _unpack_slurmdb_stats(slurmdb_stats_t *stats,
				 uint16_t protocol_version, Buf buffer)
{
	xassert(stats);
	xassert(buffer);

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack64(&stats->vsize_max, buffer);
		safe_unpack64(&stats->rss_max, buffer);
		safe_unpack64(&stats->pages_max, buffer);
		safe_unpack64(&stats->consumed_energy, buffer);

		safe_unpack32(&stats->cpu_min, buffer);

		safe_unpackdouble(&stats->vsize_ave, buffer);
		safe_unpackdouble(&stats->rss_ave, buffer);
		safe_unpackdouble(&stats->pages_ave, buffer);
		safe_unpackdouble(&stats->cpu_ave, buffer);
		safe_unpackdouble(&stats->act_cpufreq, buffer);
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
	}else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		double consumed_energy;

		safe_unpack64(&stats->vsize_max, buffer);
		safe_unpack64(&stats->rss_max, buffer);
		safe_unpack64(&stats->pages_max, buffer);
		safe_unpack32(&stats->cpu_min, buffer);

		safe_unpackdouble(&stats->vsize_ave, buffer);
		safe_unpackdouble(&stats->rss_ave, buffer);
		safe_unpackdouble(&stats->pages_ave, buffer);
		safe_unpackdouble(&stats->cpu_ave, buffer);
		safe_unpackdouble(&stats->act_cpufreq, buffer);

		safe_unpackdouble(&consumed_energy, buffer);
		stats->consumed_energy = (uint64_t)consumed_energy;

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
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	memset(stats, 0, sizeof(slurmdb_stats_t));
       	return SLURM_ERROR;
}


extern void slurmdb_pack_user_rec(void *in, uint16_t protocol_version,
				  Buf buffer)
{
	ListIterator itr = NULL;
	slurmdb_user_rec_t *object = (slurmdb_user_rec_t *)in;
	uint32_t count = NO_VAL;
	slurmdb_coord_rec_t *coord = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	slurmdb_wckey_rec_t *wckey = NULL;

	xassert(buffer);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->assoc_list);
			while ((assoc = list_next(itr))) {
				slurmdb_pack_assoc_rec(assoc, protocol_version,
						       buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->coord_accts)
			count = list_count(object->coord_accts);

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->coord_accts);
			while ((coord = list_next(itr))) {
				slurmdb_pack_coord_rec(coord,
						       protocol_version, buffer);
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->wckey_list);
			while ((wckey = list_next(itr))) {
				slurmdb_pack_wckey_rec(wckey, protocol_version,
						       buffer);
			}
			list_iterator_destroy(itr);
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_user_rec(void **object, uint16_t protocol_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_user_rec_t *object_ptr = xmalloc(sizeof(slurmdb_user_rec_t));
	uint32_t count = NO_VAL;
	slurmdb_coord_rec_t *coord = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	slurmdb_wckey_rec_t *wckey = NULL;
	int i;

	xassert(object);
	xassert(buffer);

	*object = object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&object_ptr->admin_level, buffer);
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->assoc_list =
				list_create(slurmdb_destroy_assoc_rec);
			for (i = 0; i < count; i++) {
				if (slurmdb_unpack_assoc_rec(
					    (void *)&assoc, protocol_version,
					    buffer)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->assoc_list, assoc);
			}
		}
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->coord_accts =
				list_create(slurmdb_destroy_coord_rec);
			for (i = 0; i < count; i++) {
				if (slurmdb_unpack_coord_rec(
					    (void *)&coord, protocol_version,
					    buffer)
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
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->wckey_list =
				list_create(slurmdb_destroy_wckey_rec);
			for (i = 0; i < count; i++) {
				if (slurmdb_unpack_wckey_rec(
					    (void *)&wckey, protocol_version,
					    buffer)
				    == SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->wckey_list, wckey);
			}
		}

	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_user_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_used_limits(void *in, uint32_t tres_cnt,
				     uint16_t protocol_version, Buf buffer)
{
	slurmdb_used_limits_t *object = (slurmdb_used_limits_t *)in;

	xassert(buffer);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			packnull(buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack64_array(NULL, 0, buffer);
			pack64_array(NULL, 0, buffer);
			pack32(0, buffer);
			return;
		}

		packstr(object->acct, buffer);
		pack32(object->jobs, buffer);
		pack32(object->submit_jobs, buffer);
		pack64_array(object->tres, tres_cnt, buffer);
		pack64_array(object->tres_run_mins, tres_cnt, buffer);
		pack32(object->uid, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_used_limits(void **object, uint32_t tres_cnt,
				      uint16_t protocol_version, Buf buffer)
{
	slurmdb_used_limits_t *object_ptr =
		xmalloc(sizeof(slurmdb_used_limits_t));
	uint32_t tmp32;

	xassert(object);
	xassert(buffer);

	*object = (void *)object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object_ptr->acct, &tmp32, buffer);
		safe_unpack32(&object_ptr->jobs, buffer);
		safe_unpack32(&object_ptr->submit_jobs, buffer);
		safe_unpack64_array(&object_ptr->tres, &tmp32, buffer);
		if (tmp32 != tres_cnt)
			goto unpack_error;
		safe_unpack64_array(&object_ptr->tres_run_mins, &tmp32, buffer);
		if (tmp32 != tres_cnt)
			goto unpack_error;

		safe_unpack32(&object_ptr->uid, buffer);
	} else {
		error("%s: too old of a version %u", __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_used_limits(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_account_rec(void *in, uint16_t protocol_version,
				     Buf buffer)
{
	slurmdb_coord_rec_t *coord = NULL;
	ListIterator itr = NULL;
	uint32_t count = NO_VAL;
	slurmdb_account_rec_t *object = (slurmdb_account_rec_t *)in;
	slurmdb_assoc_rec_t *assoc = NULL;

	xassert(buffer);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->assoc_list);
			while ((assoc = list_next(itr))) {
				slurmdb_pack_assoc_rec(assoc, protocol_version,
						       buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->coordinators)
			count = list_count(object->coordinators);

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->coordinators);
			while ((coord = list_next(itr))) {
				slurmdb_pack_coord_rec(coord,
						       protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}

		packstr(object->description, buffer);
		packstr(object->name, buffer);
		packstr(object->organization, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_account_rec(void **object, uint16_t protocol_version,
				      Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_coord_rec_t *coord = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	slurmdb_account_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_account_rec_t));

	xassert(object);
	xassert(buffer);

	*object = object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->assoc_list =
				list_create(slurmdb_destroy_assoc_rec);
			for(i=0; i<count; i++) {
				if (slurmdb_unpack_assoc_rec(
					    (void *)&assoc, protocol_version,
					    buffer)
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
					    (void *)&coord, protocol_version,
					    buffer)
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
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_account_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_coord_rec(void *in, uint16_t protocol_version,
				   Buf buffer)
{
	slurmdb_coord_rec_t *object = (slurmdb_coord_rec_t *)in;

	xassert(buffer);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			packnull(buffer);
			pack16(0, buffer);
			return;
		}

		packstr(object->name, buffer);
		pack16(object->direct, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_coord_rec(void **object, uint16_t protocol_version,
				    Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_coord_rec_t *object_ptr = xmalloc(sizeof(slurmdb_coord_rec_t));

	xassert(object);
	xassert(buffer);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		*object = object_ptr;
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpack16(&object_ptr->direct, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_coord_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_cluster_accounting_rec(void *in,
						uint16_t protocol_version,
						Buf buffer)
{
	slurmdb_cluster_accounting_rec_t *object =
		(slurmdb_cluster_accounting_rec_t *)in;

	xassert(buffer);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			pack64(0, buffer);
			slurmdb_pack_tres_rec(NULL, protocol_version, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack64(0, buffer);
			pack_time(0, buffer);
			pack64(0, buffer);
			return;
		}

		pack64(object->alloc_secs, buffer);
		slurmdb_pack_tres_rec(&object->tres_rec, protocol_version,
				      buffer);
		pack64(object->down_secs, buffer);
		pack64(object->idle_secs, buffer);
		pack64(object->over_secs, buffer);
		pack64(object->pdown_secs, buffer);
		pack_time(object->period_start, buffer);
		pack64(object->resv_secs, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_cluster_accounting_rec(void **object,
						 uint16_t protocol_version,
						 Buf buffer)
{
	slurmdb_cluster_accounting_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_cluster_accounting_rec_t));

	xassert(object);
	xassert(buffer);

	*object = object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack64(&object_ptr->alloc_secs, buffer);
		if (slurmdb_unpack_tres_rec_noalloc(
			    &object_ptr->tres_rec, protocol_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack64(&object_ptr->down_secs, buffer);
		safe_unpack64(&object_ptr->idle_secs, buffer);
		safe_unpack64(&object_ptr->over_secs, buffer);
		safe_unpack64(&object_ptr->pdown_secs, buffer);
		safe_unpack_time(&object_ptr->period_start, buffer);
		safe_unpack64(&object_ptr->resv_secs, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_cluster_accounting_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_clus_res_rec(void *in, uint16_t protocol_version,
				      Buf buffer)
{
	slurmdb_clus_res_rec_t *object = (slurmdb_clus_res_rec_t *)in;

	xassert(buffer);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			packnull(buffer);
			pack16(NO_VAL16, buffer);
			return;
		}
		packstr(object->cluster, buffer);
		pack16(object->percent_allowed, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_clus_res_rec(void **object, uint16_t protocol_version,
				       Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_clus_res_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_clus_res_rec_t));

	xassert(object);
	xassert(buffer);

	*object = object_ptr;

	slurmdb_init_clus_res_rec(object_ptr, 0);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp,
				       buffer);
		safe_unpack16(&object_ptr->percent_allowed, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_clus_res_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_cluster_rec(void *in, uint16_t protocol_version,
				     Buf buffer)
{
	slurmdb_cluster_accounting_rec_t *slurmdb_info = NULL;
	ListIterator itr = NULL;
	uint32_t count = NO_VAL;
	slurmdb_cluster_rec_t *object = (slurmdb_cluster_rec_t *)in;
	slurm_persist_conn_t *persist_conn;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			packnull(buffer);
			pack32(0, buffer);
			pack16(1, buffer);

			pack32(NO_VAL, buffer);
			packnull(buffer);
			pack32(0, buffer);
			pack32(0, buffer);

			pack32(NO_VAL, buffer);

			packnull(buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);

			slurmdb_pack_assoc_rec(NULL, protocol_version, buffer);

			pack16(0, buffer);
			pack8(0, buffer);
			pack8(0, buffer);
			packnull(buffer);
			return;
		}

		if (!object->accounting_list ||
		    !(count = list_count(object->accounting_list)))
			count = NO_VAL;

		pack32(count, buffer);

		if (count != NO_VAL) {
			itr = list_iterator_create(object->accounting_list);
			while ((slurmdb_info = list_next(itr))) {
				slurmdb_pack_cluster_accounting_rec(
					slurmdb_info, protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->classification, buffer);
		packstr(object->control_host, buffer);
		pack32(object->control_port, buffer);
		pack16(object->dimensions, buffer);

		if (object->fed.feature_list)
			count = list_count(object->fed.feature_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			char *tmp_feature;
			itr = list_iterator_create(object->fed.feature_list);
			while ((tmp_feature = list_next(itr)))
				packstr(tmp_feature, buffer);
			list_iterator_destroy(itr);
		}
		packstr(object->fed.name, buffer);
		pack32(object->fed.id, buffer);
		pack32(object->fed.state, buffer);

		pack32(object->flags, buffer);

		packstr(object->name, buffer);
		packstr(object->nodes, buffer);

		pack32(object->plugin_id_select, buffer);

		slurmdb_pack_assoc_rec(object->root_assoc,
				       protocol_version, buffer);

		pack16(object->rpc_version, buffer);
		persist_conn = object->fed.recv;
		pack8((persist_conn && persist_conn->fd != -1) ? 1 : 0, buffer);
		persist_conn = object->fed.send;
		pack8((persist_conn && persist_conn->fd != -1) ? 1 : 0, buffer);
		packstr(object->tres_str, buffer);
	} else if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			packnull(buffer);
			pack32(0, buffer);
			pack16(1, buffer);

			packnull(buffer);
			pack32(0, buffer);
			pack32(0, buffer);
			pack32(0, buffer);

			pack32(NO_VAL, buffer);

			packnull(buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);

			slurmdb_pack_assoc_rec(NULL, protocol_version, buffer);

			pack16(0, buffer);
			pack8(0, buffer);
			pack8(0, buffer);
			packnull(buffer);
			return;
		}

		if (!object->accounting_list ||
		    !(count = list_count(object->accounting_list)))
			count = NO_VAL;

		pack32(count, buffer);

		if (count != NO_VAL) {
			itr = list_iterator_create(object->accounting_list);
			while ((slurmdb_info = list_next(itr))) {
				slurmdb_pack_cluster_accounting_rec(
					slurmdb_info, protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->classification, buffer);
		packstr(object->control_host, buffer);
		pack32(object->control_port, buffer);
		pack16(object->dimensions, buffer);

		packstr(object->fed.name, buffer);
		pack32(object->fed.id, buffer);
		pack32(object->fed.state, buffer);
		pack32(NO_VAL, buffer);

		pack32(object->flags, buffer);

		packstr(object->name, buffer);
		packstr(object->nodes, buffer);

		pack32(object->plugin_id_select, buffer);

		slurmdb_pack_assoc_rec(object->root_assoc,
				       protocol_version, buffer);

		pack16(object->rpc_version, buffer);
		persist_conn = object->fed.recv;
		pack8((persist_conn && persist_conn->fd != -1) ? 1 : 0, buffer);
		persist_conn = object->fed.send;
		pack8((persist_conn && persist_conn->fd != -1) ? 1 : 0, buffer);
		packstr(object->tres_str, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			packnull(buffer);
			pack32(0, buffer);
			pack16(1, buffer);
			pack32(NO_VAL, buffer);

			packnull(buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);

			slurmdb_pack_assoc_rec(NULL, protocol_version, buffer);

			pack16(0, buffer);
			packnull(buffer);
			return;
		}

		if (!object->accounting_list ||
		    !(count = list_count(object->accounting_list)))
			count = NO_VAL;

		pack32(count, buffer);

		if (count != NO_VAL) {
			itr = list_iterator_create(object->accounting_list);
			while ((slurmdb_info = list_next(itr))) {
				slurmdb_pack_cluster_accounting_rec(
					slurmdb_info, protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->classification, buffer);
		packstr(object->control_host, buffer);
		pack32(object->control_port, buffer);
		pack16(object->dimensions, buffer);
		pack32(object->flags, buffer);

		packstr(object->name, buffer);
		packstr(object->nodes, buffer);

		pack32(object->plugin_id_select, buffer);

		slurmdb_pack_assoc_rec(object->root_assoc,
				       protocol_version, buffer);

		pack16(object->rpc_version, buffer);
		packstr(object->tres_str, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_cluster_rec(void **object, uint16_t protocol_version,
				      Buf buffer)
{
	uint32_t uint32_tmp;
	uint8_t uint8_tmp;
	int i;
	uint32_t count;
	slurmdb_cluster_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_cluster_rec_t));
	slurmdb_cluster_accounting_rec_t *slurmdb_info = NULL;
	slurm_persist_conn_t *conn;

	*object = object_ptr;

	slurmdb_init_cluster_rec(object_ptr, 0);
	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->accounting_list = list_create(
				slurmdb_destroy_cluster_accounting_rec);
			for (i = 0; i < count; i++) {
				if (slurmdb_unpack_cluster_accounting_rec(
					    (void *)&slurmdb_info,
					    protocol_version, buffer) ==
				    SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->accounting_list,
					    slurmdb_info);
			}
		}

		safe_unpack16(&object_ptr->classification, buffer);
		safe_unpackstr_xmalloc(&object_ptr->control_host,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->control_port, buffer);
		safe_unpack16(&object_ptr->dimensions, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->fed.feature_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				char *tmp_feature = NULL;
				safe_unpackstr_xmalloc(&tmp_feature,
						       &uint32_tmp, buffer);
				list_append(object_ptr->fed.feature_list,
					    tmp_feature);
			}
		}
		safe_unpackstr_xmalloc(&object_ptr->fed.name,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->fed.id, buffer);
		safe_unpack32(&object_ptr->fed.state, buffer);

		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->nodes, &uint32_tmp, buffer);

		safe_unpack32(&object_ptr->plugin_id_select, buffer);

		if (slurmdb_unpack_assoc_rec(
			    (void **)&object_ptr->root_assoc,
			    protocol_version, buffer)
		    == SLURM_ERROR)
			goto unpack_error;

		safe_unpack16(&object_ptr->rpc_version, buffer);
		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			conn = xmalloc(sizeof(slurm_persist_conn_t));
			conn->fd = -1;
			object_ptr->fed.recv = conn;
		}
		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			conn = xmalloc(sizeof(slurm_persist_conn_t));
			conn->fd = -1;
			object_ptr->fed.send = conn;
		}
		safe_unpackstr_xmalloc(&object_ptr->tres_str,
				       &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->accounting_list = list_create(
				slurmdb_destroy_cluster_accounting_rec);
			for (i = 0; i < count; i++) {
				if (slurmdb_unpack_cluster_accounting_rec(
					    (void *)&slurmdb_info,
					    protocol_version, buffer) ==
				    SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->accounting_list,
					    slurmdb_info);
			}
		}

		safe_unpack16(&object_ptr->classification, buffer);
		safe_unpackstr_xmalloc(&object_ptr->control_host,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->control_port, buffer);
		safe_unpack16(&object_ptr->dimensions, buffer);

		safe_unpackstr_xmalloc(&object_ptr->fed.name,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->fed.id, buffer);
		safe_unpack32(&object_ptr->fed.state, buffer);
		safe_unpack32(&uint32_tmp, buffer);

		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->nodes, &uint32_tmp, buffer);

		safe_unpack32(&object_ptr->plugin_id_select, buffer);

		if (slurmdb_unpack_assoc_rec(
			    (void **)&object_ptr->root_assoc,
			    protocol_version, buffer)
		    == SLURM_ERROR)
			goto unpack_error;

		safe_unpack16(&object_ptr->rpc_version, buffer);
		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			conn = xmalloc(sizeof(slurm_persist_conn_t));
			conn->fd = -1;
			object_ptr->fed.recv = conn;
		}
		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp) {
			conn = xmalloc(sizeof(slurm_persist_conn_t));
			conn->fd = -1;
			object_ptr->fed.send = conn;
		}
		safe_unpackstr_xmalloc(&object_ptr->tres_str,
				       &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->accounting_list = list_create(
				slurmdb_destroy_cluster_accounting_rec);
			for (i = 0; i < count; i++) {
				if (slurmdb_unpack_cluster_accounting_rec(
					    (void *)&slurmdb_info,
					    protocol_version, buffer) ==
				    SLURM_ERROR)
					goto unpack_error;
				list_append(object_ptr->accounting_list,
					    slurmdb_info);
			}
		}

		safe_unpack16(&object_ptr->classification, buffer);
		safe_unpackstr_xmalloc(&object_ptr->control_host,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->control_port, buffer);
		safe_unpack16(&object_ptr->dimensions, buffer);
		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->nodes, &uint32_tmp, buffer);

		safe_unpack32(&object_ptr->plugin_id_select, buffer);

		if (slurmdb_unpack_assoc_rec(
			    (void **)&object_ptr->root_assoc,
			    protocol_version, buffer)
		    == SLURM_ERROR)
			goto unpack_error;

		safe_unpack16(&object_ptr->rpc_version, buffer);
		safe_unpackstr_xmalloc(&object_ptr->tres_str,
				       &uint32_tmp, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	/* Take the lower of the remote cluster is using and what I am or I
	 * won't be able to talk to the remote cluster. domo arigato. */
	object_ptr->rpc_version = MIN(SLURM_PROTOCOL_VERSION,
				      object_ptr->rpc_version);

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_cluster_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_federation_rec(void *in, uint16_t protocol_version,
					Buf buffer)
{
	uint32_t count   = NO_VAL;
	ListIterator itr = NULL;
	slurmdb_cluster_rec_t *tmp_cluster = NULL;
	slurmdb_federation_rec_t *object = (slurmdb_federation_rec_t *)in;

	xassert(buffer);

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (!object) {
			pack8(0, buffer); /* NULL */
			return;
		}
		pack8(1, buffer); /* Not NULL */
		packstr(object->name, buffer);
		pack32(object->flags, buffer);

		if (object->cluster_list)
			count = list_count(object->cluster_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_cluster = list_next(itr))) {
				slurmdb_pack_cluster_rec(tmp_cluster,
							 protocol_version,
							 buffer);
			}
			list_iterator_destroy(itr);
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			packnull(buffer);
			pack32(0, buffer);
			pack32(NO_VAL, buffer);
			return;
		}
		packstr(object->name, buffer);
		pack32(object->flags, buffer);

		if (object->cluster_list)
			count = list_count(object->cluster_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_cluster = list_next(itr))) {
				slurmdb_pack_cluster_rec(tmp_cluster,
							 protocol_version,
							 buffer);
			}
			list_iterator_destroy(itr);
		}
	} else {
		error("%s: protocol_version %hu not supported.",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_federation_rec(void **object,
					 uint16_t protocol_version, Buf buffer)
{
	uint8_t  uint8_tmp;
	uint32_t uint32_tmp;
	uint32_t count;
	int      i;
	slurmdb_cluster_rec_t *tmp_cluster = NULL;
	slurmdb_federation_rec_t *object_ptr = NULL;

	xassert(object);
	xassert(buffer);

	*object = NULL;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack8(&uint8_tmp, buffer);
		if (!uint8_tmp) /* NULL fed_rec */
			return SLURM_SUCCESS;

		object_ptr = xmalloc(sizeof(slurmdb_federation_rec_t));
		slurmdb_init_federation_rec(object_ptr, 0);
		*object = object_ptr;

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurmdb_destroy_cluster_rec);
			for(i = 0; i < count; i++) {
				if (slurmdb_unpack_cluster_rec(
						(void **)&tmp_cluster,
						protocol_version, buffer)
				    != SLURM_SUCCESS) {
					error("unpacking cluster_rec");
					goto unpack_error;
				}
				list_append(object_ptr->cluster_list,
					    tmp_cluster);
			}
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		object_ptr = xmalloc(sizeof(slurmdb_federation_rec_t));
		slurmdb_init_federation_rec(object_ptr, 0);
		*object = object_ptr;

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurmdb_destroy_cluster_rec);
			for(i = 0; i < count; i++) {
				if (slurmdb_unpack_cluster_rec(
						(void **)&tmp_cluster,
						protocol_version, buffer)
				    != SLURM_SUCCESS) {
					error("unpacking cluster_rec");
					goto unpack_error;
				}
				list_append(object_ptr->cluster_list,
					    tmp_cluster);
			}
		}
	} else {
		error("%s: protocol_version %hu is not supported.",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_federation_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_accounting_rec(void *in, uint16_t protocol_version,
					Buf buffer)
{
	slurmdb_accounting_rec_t *object = (slurmdb_accounting_rec_t *)in;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			pack64(0, buffer);
			slurmdb_pack_tres_rec(NULL, protocol_version, buffer);
			pack32(0, buffer);
			pack_time(0, buffer);
			return;
		}

		pack64(object->alloc_secs, buffer);
		slurmdb_pack_tres_rec(&object->tres_rec,
				      protocol_version, buffer);
		pack32(object->id, buffer);
		pack_time(object->period_start, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_accounting_rec(void **object,
					 uint16_t protocol_version,
					 Buf buffer)
{
	slurmdb_accounting_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_accounting_rec_t));

	*object = object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack64(&object_ptr->alloc_secs, buffer);
		if (slurmdb_unpack_tres_rec_noalloc(
			    &object_ptr->tres_rec, protocol_version, buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&object_ptr->id, buffer);
		safe_unpack_time(&object_ptr->period_start, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_accounting_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_assoc_rec(void *in, uint16_t protocol_version,
				   Buf buffer)
{
	slurmdb_accounting_rec_t *slurmdb_info = NULL;
	ListIterator itr = NULL;
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	slurmdb_assoc_rec_t *object = (slurmdb_assoc_rec_t *)in;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);

			pack32(NO_VAL, buffer);

			pack32(NO_VAL, buffer);

			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			pack32(0, buffer);
			pack16(0, buffer);
			pack32(0, buffer);

			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
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

		if (!object->accounting_list ||
		    !(count = list_count(object->accounting_list)))
			count = NO_VAL;

		pack32(count, buffer);

		if (count != NO_VAL) {
			itr = list_iterator_create(object->accounting_list);
			while ((slurmdb_info = list_next(itr))) {
				slurmdb_pack_accounting_rec(slurmdb_info,
							    protocol_version,
							    buffer);
			}
			list_iterator_destroy(itr);
		}


		packstr(object->acct, buffer);
		packstr(object->cluster, buffer);

		pack32(object->def_qos_id, buffer);

		/* this used to be named fairshare to not have to redo
		   the order of things just to be in alpha order we
		   just renamed it and called it good */
		pack32(object->shares_raw, buffer);

		packstr(object->grp_tres_mins, buffer);
		packstr(object->grp_tres_run_mins, buffer);
		packstr(object->grp_tres, buffer);
		pack32(object->grp_jobs, buffer);
		pack32(object->grp_submit_jobs, buffer);
		pack32(object->grp_wall, buffer);

		pack32(object->id, buffer);
		pack16(object->is_def, buffer);
		pack32(object->lft, buffer);

		packstr(object->max_tres_mins_pj, buffer);
		packstr(object->max_tres_run_mins, buffer);
		packstr(object->max_tres_pj, buffer);
		packstr(object->max_tres_pn, buffer);
		pack32(object->max_jobs, buffer);
		pack32(object->max_submit_jobs, buffer);
		pack32(object->max_wall_pj, buffer);

		packstr(object->parent_acct, buffer);
		pack32(object->parent_id, buffer);
		packstr(object->partition, buffer);

		if (object->qos_list)
			count = list_count(object->qos_list);

		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->qos_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack32(object->rgt, buffer);
		pack32(object->uid, buffer);

		packstr(object->user, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_assoc_rec_members(slurmdb_assoc_rec_t *object_ptr,
					    uint16_t protocol_version,
					    Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	char *tmp_info = NULL;
	slurmdb_accounting_rec_t *slurmdb_info = NULL;

	slurmdb_init_assoc_rec(object_ptr, 0);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->accounting_list =
				list_create(slurmdb_destroy_accounting_rec);
			for (i = 0; i < count; i++) {
				if (slurmdb_unpack_accounting_rec(
					    (void **)&slurmdb_info,
					    protocol_version,
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

		safe_unpackstr_xmalloc(&object_ptr->grp_tres_mins,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->grp_tres_run_mins,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->grp_tres,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->grp_jobs, buffer);
		safe_unpack32(&object_ptr->grp_submit_jobs, buffer);
		safe_unpack32(&object_ptr->grp_wall, buffer);

		safe_unpack32(&object_ptr->id, buffer);
		safe_unpack16(&object_ptr->is_def, buffer);
		safe_unpack32(&object_ptr->lft, buffer);

		safe_unpackstr_xmalloc(&object_ptr->max_tres_mins_pj,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->max_tres_run_mins,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->max_tres_pj,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->max_tres_pn,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->max_jobs, buffer);
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
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:

	return SLURM_ERROR;
}

extern int slurmdb_unpack_assoc_rec(void **object, uint16_t protocol_version,
				    Buf buffer)
{
	int rc;
	slurmdb_assoc_rec_t *object_ptr = xmalloc(sizeof(slurmdb_assoc_rec_t));

	*object = object_ptr;

	slurmdb_init_assoc_rec(object_ptr, 0);

	if ((rc = slurmdb_unpack_assoc_rec_members(
		     object_ptr, protocol_version, buffer)) != SLURM_SUCCESS) {
		slurmdb_destroy_assoc_rec(object_ptr);
		*object = NULL;
	}
	return rc;
}

extern void slurmdb_pack_assoc_usage(void *in, uint16_t protocol_version,
				     Buf buffer)
{
	slurmdb_assoc_usage_t *usage = (slurmdb_assoc_usage_t *)in;

	xassert(buffer);
        xassert(usage);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack64_array(usage->grp_used_tres, usage->tres_cnt, buffer);
		pack64_array(usage->grp_used_tres_run_secs, usage->tres_cnt,
			     buffer);
		packdouble(usage->grp_used_wall, buffer);
		packdouble(usage->fs_factor, buffer);
		pack32(usage->level_shares, buffer);
		packdouble(usage->shares_norm, buffer);
		packlongdouble(usage->usage_efctv, buffer);
		packlongdouble(usage->usage_norm, buffer);
		packlongdouble(usage->usage_raw, buffer);
		packlongdouble_array(usage->usage_tres_raw, usage->tres_cnt,
				     buffer);
		pack32(usage->used_jobs, buffer);
		pack32(usage->used_submit_jobs, buffer);
		packlongdouble(usage->level_fs, buffer);
		pack_bit_str_hex(usage->valid_qos, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_assoc_usage(void **object, uint16_t protocol_version,
				      Buf buffer)
{
	slurmdb_assoc_usage_t *object_ptr =
		xmalloc(sizeof(slurmdb_assoc_usage_t));
	uint32_t tmp32;
	*object = object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack64_array(&object_ptr->grp_used_tres, &tmp32, buffer);
		object_ptr->tres_cnt = tmp32;
		safe_unpack64_array(&object_ptr->grp_used_tres_run_secs,
				    &tmp32, buffer);
		safe_unpackdouble(&object_ptr->grp_used_wall, buffer);
		safe_unpackdouble(&object_ptr->fs_factor, buffer);
		safe_unpack32(&object_ptr->level_shares, buffer);
		safe_unpackdouble(&object_ptr->shares_norm, buffer);
		safe_unpacklongdouble(&object_ptr->usage_efctv, buffer);
		safe_unpacklongdouble(&object_ptr->usage_norm, buffer);
		safe_unpacklongdouble(&object_ptr->usage_raw, buffer);
		safe_unpacklongdouble_array(&object_ptr->usage_tres_raw,
					    &tmp32, buffer);

		safe_unpack32(&object_ptr->used_jobs, buffer);
		safe_unpack32(&object_ptr->used_submit_jobs, buffer);
		safe_unpacklongdouble(&object_ptr->level_fs, buffer);
		unpack_bit_str_hex(&object_ptr->valid_qos, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_assoc_usage(object_ptr);
	*object = NULL;

	return SLURM_ERROR;
}

extern void slurmdb_pack_assoc_rec_with_usage(void *in,
					      uint16_t protocol_version,
					      Buf buffer)
{
	slurmdb_assoc_rec_t *object = (slurmdb_assoc_rec_t *)in;

	slurmdb_pack_assoc_rec(in, protocol_version, buffer);
	slurmdb_pack_assoc_usage(object->usage, protocol_version, buffer);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack64_array(object->grp_tres_mins_ctld,
			     object->usage->tres_cnt, buffer);
		pack64_array(object->grp_tres_run_mins_ctld,
			     object->usage->tres_cnt, buffer);
		pack64_array(object->grp_tres_ctld,
			     object->usage->tres_cnt, buffer);

		pack64_array(object->max_tres_mins_ctld,
			     object->usage->tres_cnt, buffer);
		pack64_array(object->max_tres_run_mins_ctld,
			     object->usage->tres_cnt, buffer);
		pack64_array(object->max_tres_ctld,
			     object->usage->tres_cnt, buffer);
		pack64_array(object->max_tres_pn_ctld,
		     object->usage->tres_cnt, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}

}

extern int slurmdb_unpack_assoc_rec_with_usage(void **object,
					       uint16_t protocol_version,
					       Buf buffer)
{
	int rc;
	uint32_t uint32_tmp;
	slurmdb_assoc_rec_t *object_ptr;

	xassert(object);
	xassert(buffer);

	if ((rc = slurmdb_unpack_assoc_rec(object, protocol_version, buffer))
	    != SLURM_SUCCESS)
		return rc;

	object_ptr = *object;

	if ((rc = slurmdb_unpack_assoc_usage((void **)&object_ptr->usage,
					     protocol_version, buffer)))
		goto unpack_error;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack64_array(&object_ptr->grp_tres_mins_ctld,
				    &uint32_tmp, buffer);
		safe_unpack64_array(&object_ptr->grp_tres_run_mins_ctld,
				    &uint32_tmp, buffer);
		safe_unpack64_array(&object_ptr->grp_tres_ctld,
				    &uint32_tmp, buffer);

		safe_unpack64_array(&object_ptr->max_tres_mins_ctld,
				    &uint32_tmp, buffer);
		safe_unpack64_array(&object_ptr->max_tres_run_mins_ctld,
				    &uint32_tmp, buffer);
		safe_unpack64_array(&object_ptr->max_tres_ctld,
				    &uint32_tmp, buffer);
		safe_unpack64_array(&object_ptr->max_tres_pn_ctld,
				    &uint32_tmp, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return rc;

unpack_error:
	slurmdb_destroy_assoc_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_event_rec(void *in, uint16_t protocol_version,
				   Buf buffer)
{
	slurmdb_event_rec_t *object = (slurmdb_event_rec_t *)in;

	xassert(buffer);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			packnull(buffer);
			packnull(buffer);
			pack16(0, buffer);
			packnull(buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			packnull(buffer);
			pack32(NO_VAL, buffer);
			pack16(NO_VAL16, buffer);
			packnull(buffer);
			return;
		}

		packstr(object->cluster, buffer);
		packstr(object->cluster_nodes, buffer);
		pack16(object->event_type, buffer);
		packstr(object->node_name, buffer);
		pack_time(object->period_start, buffer);
		pack_time(object->period_end, buffer);
		packstr(object->reason, buffer);
		pack32(object->reason_uid, buffer);
		pack16(object->state, buffer);
		packstr(object->tres_str, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_event_rec(void **object, uint16_t protocol_version,
				    Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_event_rec_t *object_ptr = xmalloc(sizeof(slurmdb_event_rec_t));

	xassert(buffer);
	xassert(object);

	*object = object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object_ptr->cluster,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->cluster_nodes,
				       &uint32_tmp, buffer);
		safe_unpack16(&object_ptr->event_type, buffer);
		safe_unpackstr_xmalloc(&object_ptr->node_name,
				       &uint32_tmp, buffer);
		safe_unpack_time(&object_ptr->period_start, buffer);
		safe_unpack_time(&object_ptr->period_end, buffer);
		safe_unpackstr_xmalloc(&object_ptr->reason,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->reason_uid, buffer);
		safe_unpack16(&object_ptr->state, buffer);
		safe_unpackstr_xmalloc(&object_ptr->tres_str,
				       &uint32_tmp, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_event_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_qos_rec(void *in, uint16_t protocol_version, Buf buffer)
{
	ListIterator itr = NULL;
	slurmdb_qos_rec_t *object = (slurmdb_qos_rec_t *)in;
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			packnull(buffer);
			pack32(0, buffer);

			pack32(QOS_FLAG_NOTSET, buffer);

			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);

			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			packnull(buffer);

			packnull(buffer);

			pack_bit_str_hex(NULL, buffer);
			pack32(NO_VAL, buffer);

			pack16(0, buffer);
			pack32(0, buffer);

			packdouble(NO_VAL64, buffer);
			packdouble(NO_VAL64, buffer);
			return;
		}
		packstr(object->description, buffer);
		pack32(object->id, buffer);

		pack32(object->flags, buffer);

		pack32(object->grace_time, buffer);
		packstr(object->grp_tres_mins, buffer);
		packstr(object->grp_tres_run_mins, buffer);
		packstr(object->grp_tres, buffer);
		pack32(object->grp_jobs, buffer);
		pack32(object->grp_submit_jobs, buffer);
		pack32(object->grp_wall, buffer);

		packstr(object->max_tres_mins_pj, buffer);
		packstr(object->max_tres_run_mins_pa, buffer);
		packstr(object->max_tres_run_mins_pu, buffer);
		packstr(object->max_tres_pa, buffer);
		packstr(object->max_tres_pj, buffer);
		packstr(object->max_tres_pn, buffer);
		packstr(object->max_tres_pu, buffer);
		pack32(object->max_jobs_pa, buffer);
		pack32(object->max_jobs_pu, buffer);
		pack32(object->max_submit_jobs_pa, buffer);
		pack32(object->max_submit_jobs_pu, buffer);
		pack32(object->max_wall_pj, buffer);
		packstr(object->min_tres_pj, buffer);

		packstr(object->name, buffer);

		pack_bit_str_hex(object->preempt_bitstr, buffer);

		if (object->preempt_list)
			count = list_count(object->preempt_list);

		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
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
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_qos_rec(void **object, uint16_t protocol_version,
				  Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	slurmdb_qos_rec_t *object_ptr = xmalloc(sizeof(slurmdb_qos_rec_t));
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;

	*object = object_ptr;

	slurmdb_init_qos_rec(object_ptr, 0, NO_VAL);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object_ptr->description,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->id, buffer);

		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpack32(&object_ptr->grace_time, buffer);
		safe_unpackstr_xmalloc(&object_ptr->grp_tres_mins,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->grp_tres_run_mins,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->grp_tres,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->grp_jobs, buffer);
		safe_unpack32(&object_ptr->grp_submit_jobs, buffer);
		safe_unpack32(&object_ptr->grp_wall, buffer);

		safe_unpackstr_xmalloc(&object_ptr->max_tres_mins_pj,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->max_tres_run_mins_pa,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->max_tres_run_mins_pu,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->max_tres_pa,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->max_tres_pj,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->max_tres_pn,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->max_tres_pu,
				       &uint32_tmp, buffer);
		safe_unpack32(&object_ptr->max_jobs_pa, buffer);
		safe_unpack32(&object_ptr->max_jobs_pu, buffer);
		safe_unpack32(&object_ptr->max_submit_jobs_pa, buffer);
		safe_unpack32(&object_ptr->max_submit_jobs_pu, buffer);
		safe_unpack32(&object_ptr->max_wall_pj, buffer);
		safe_unpackstr_xmalloc(&object_ptr->min_tres_pj,
				       &uint32_tmp, buffer);

		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);

		unpack_bit_str_hex(&object_ptr->preempt_bitstr, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
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
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_qos_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_qos_usage(void *in, uint16_t protocol_version,
				   Buf buffer)
{
	slurmdb_qos_usage_t *usage = (slurmdb_qos_usage_t *)in;
	uint32_t count;
	ListIterator itr;
	void *used_limits;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(usage->grp_used_jobs, buffer);
		pack32(usage->grp_used_submit_jobs, buffer);
		pack64_array(usage->grp_used_tres, usage->tres_cnt, buffer);
		pack64_array(usage->grp_used_tres_run_secs,
			     usage->tres_cnt, buffer);
		packdouble(usage->grp_used_wall, buffer);
		packdouble(usage->norm_priority, buffer);
		packlongdouble(usage->usage_raw, buffer);
		packlongdouble_array(usage->usage_tres_raw,
				     usage->tres_cnt, buffer);

		if (!usage->user_limit_list ||
		    !(count = list_count(usage->user_limit_list)))
			count = NO_VAL;

		/* We have to pack anything that is verified by
		 * tres_cnt after this.  It is used in the unpack,
		 * that is the reason it isn't alpha.
		 */
		pack32(count, buffer);
		if (count != NO_VAL) {
			itr = list_iterator_create(usage->user_limit_list);
			while ((used_limits = list_next(itr)))
				slurmdb_pack_used_limits(
					used_limits, usage->tres_cnt,
					protocol_version, buffer);
			list_iterator_destroy(itr);
		}
		if (!usage->acct_limit_list ||
		    !(count = list_count(usage->acct_limit_list)))
			count = NO_VAL;

		pack32(count, buffer);
		if (count != NO_VAL) {
			itr = list_iterator_create(usage->acct_limit_list);
			while ((used_limits = list_next(itr)))
				slurmdb_pack_used_limits(
					used_limits, usage->tres_cnt,
					protocol_version, buffer);
			list_iterator_destroy(itr);
		}
	} else {
		error("%s: version too old %u", __func__, protocol_version);
		return;
	}

}

extern int slurmdb_unpack_qos_usage(void **object, uint16_t protocol_version,
				    Buf buffer)
{
	slurmdb_qos_usage_t *object_ptr = xmalloc(sizeof(slurmdb_qos_usage_t));

	uint32_t count;
	void *used_limits;
	int i;

	*object = object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&object_ptr->grp_used_jobs, buffer);
		safe_unpack32(&object_ptr->grp_used_submit_jobs, buffer);
		safe_unpack64_array(&object_ptr->grp_used_tres,
				    &object_ptr->tres_cnt, buffer);
		safe_unpack64_array(&object_ptr->grp_used_tres_run_secs,
				    &object_ptr->tres_cnt, buffer);
		safe_unpackdouble(&object_ptr->grp_used_wall, buffer);
		safe_unpackdouble(&object_ptr->norm_priority, buffer);
		safe_unpacklongdouble(&object_ptr->usage_raw, buffer);
		safe_unpacklongdouble_array(&object_ptr->usage_tres_raw,
					    &count, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->user_limit_list =
				list_create(slurmdb_destroy_used_limits);
			for (i = 0; i < count; i++) {
				if (slurmdb_unpack_used_limits(
					    &used_limits,
					    object_ptr->tres_cnt,
					    protocol_version, buffer)
				    != SLURM_SUCCESS)
					goto unpack_error;
				list_append(object_ptr->user_limit_list,
					    used_limits);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->acct_limit_list =
				list_create(slurmdb_destroy_used_limits);
			for (i = 0; i < count; i++) {
				if (slurmdb_unpack_used_limits(
					    &used_limits,
					    object_ptr->tres_cnt,
					    protocol_version, buffer)
				    != SLURM_SUCCESS)
					goto unpack_error;
				list_append(object_ptr->acct_limit_list,
					    used_limits);
			}
		}
	} else {
		error("%s: version too old %u", __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_qos_usage(object_ptr);
	*object = NULL;

	return SLURM_ERROR;
}

extern void slurmdb_pack_qos_rec_with_usage(void *in, uint16_t protocol_version,
					    Buf buffer)
{
	slurmdb_qos_rec_t *object = (slurmdb_qos_rec_t *)in;

	slurmdb_pack_qos_rec(in, protocol_version, buffer);

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack64_array(object->grp_tres_mins_ctld,
			     object->usage->tres_cnt, buffer);
		pack64_array(object->grp_tres_run_mins_ctld,
			     object->usage->tres_cnt, buffer);
		pack64_array(object->grp_tres_ctld,
			     object->usage->tres_cnt, buffer);

		pack64_array(object->max_tres_mins_pj_ctld,
			     object->usage->tres_cnt, buffer);
		pack64_array(object->max_tres_run_mins_pa_ctld,
			     object->usage->tres_cnt, buffer);
		pack64_array(object->max_tres_run_mins_pu_ctld,
			     object->usage->tres_cnt, buffer);
		pack64_array(object->max_tres_pa_ctld,
			     object->usage->tres_cnt, buffer);
		pack64_array(object->max_tres_pj_ctld,
			     object->usage->tres_cnt, buffer);
		pack64_array(object->max_tres_pn_ctld,
			     object->usage->tres_cnt, buffer);
		pack64_array(object->max_tres_pu_ctld,
			     object->usage->tres_cnt, buffer);
		pack64_array(object->min_tres_pj_ctld,
			     object->usage->tres_cnt, buffer);
	} else {
		error("%s: version too old %u", __func__, protocol_version);
		return;
	}

	slurmdb_pack_qos_usage(object->usage,
			       protocol_version, buffer);

}

extern int slurmdb_unpack_qos_rec_with_usage(void **object,
					     uint16_t protocol_version,
					     Buf buffer)
{
	int rc;
	slurmdb_qos_rec_t *object_ptr;
	uint32_t uint32_tmp;

	if ((rc = slurmdb_unpack_qos_rec(object, protocol_version, buffer))
	    != SLURM_SUCCESS)
		return rc;

	object_ptr = *object;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack64_array(&object_ptr->grp_tres_mins_ctld,
				    &uint32_tmp, buffer);
		safe_unpack64_array(&object_ptr->grp_tres_run_mins_ctld,
				    &uint32_tmp, buffer);
		safe_unpack64_array(&object_ptr->grp_tres_ctld,
				    &uint32_tmp, buffer);

		safe_unpack64_array(&object_ptr->max_tres_mins_pj_ctld,
				    &uint32_tmp, buffer);
		safe_unpack64_array(&object_ptr->max_tres_run_mins_pa_ctld,
				    &uint32_tmp, buffer);
		safe_unpack64_array(&object_ptr->max_tres_run_mins_pu_ctld,
				    &uint32_tmp, buffer);
		safe_unpack64_array(&object_ptr->max_tres_pa_ctld,
				    &uint32_tmp, buffer);
		safe_unpack64_array(&object_ptr->max_tres_pj_ctld,
				    &uint32_tmp, buffer);
		safe_unpack64_array(&object_ptr->max_tres_pn_ctld,
				    &uint32_tmp, buffer);
		safe_unpack64_array(&object_ptr->max_tres_pu_ctld,
				    &uint32_tmp, buffer);
		safe_unpack64_array(&object_ptr->min_tres_pj_ctld,
				    &uint32_tmp, buffer);
	} else {
		error("%s: version too old %u", __func__, protocol_version);
		goto unpack_error;
	}

	rc = slurmdb_unpack_qos_usage((void **)&object_ptr->usage,
				      protocol_version, buffer);

	return rc;

unpack_error:
	slurmdb_destroy_qos_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_reservation_rec(void *in, uint16_t protocol_version,
					 Buf buffer)
{
	slurmdb_reservation_rec_t *object = (slurmdb_reservation_rec_t *)in;
	uint32_t count = NO_VAL;
	ListIterator itr;
	slurmdb_tres_rec_t *tres_rec;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (!object) {
			packnull(buffer);
			packnull(buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			packnull(buffer);
			pack32(NO_VAL, buffer);
			packdouble(0.0, buffer);
			return;
		}

		packstr(object->assocs, buffer);
		packstr(object->cluster, buffer);
		pack32(object->flags, buffer);
		pack32(object->id, buffer);
		packstr(object->name, buffer);
		packstr(object->nodes, buffer);
		packstr(object->node_inx, buffer);
		pack_time(object->time_end, buffer);
		pack_time(object->time_start, buffer);
		pack_time(object->time_start_prev, buffer);
		packstr(object->tres_str, buffer);

		if (object->tres_list)
			count = list_count(object->tres_list);
		else
			count = NO_VAL;

		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->tres_list);
			while ((tres_rec = list_next(itr))) {
				slurmdb_pack_tres_rec(
					tres_rec, protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}
		packdouble(object->unused_wall, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			packnull(buffer);
			packnull(buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			packnull(buffer);
			packnull(buffer);
			packnull(buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			packnull(buffer);
			pack32(NO_VAL, buffer);
			return;
		}

		packstr(object->assocs, buffer);
		packstr(object->cluster, buffer);
		pack32(object->flags, buffer);
		pack32(object->id, buffer);
		packstr(object->name, buffer);
		packstr(object->nodes, buffer);
		packstr(object->node_inx, buffer);
		pack_time(object->time_end, buffer);
		pack_time(object->time_start, buffer);
		pack_time(object->time_start_prev, buffer);
		packstr(object->tres_str, buffer);

		if (object->tres_list)
			count = list_count(object->tres_list);
		else
			count = NO_VAL;

		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->tres_list);
			while ((tres_rec = list_next(itr))) {
				slurmdb_pack_tres_rec(
					tres_rec, protocol_version, buffer);
			}
			list_iterator_destroy(itr);
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_reservation_rec(void **object,
					  uint16_t protocol_version,
					  Buf buffer)
{
	uint32_t uint32_tmp, count;
	int i;
	void *tmp_info;
	slurmdb_reservation_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_reservation_rec_t));

	*object = object_ptr;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object_ptr->assocs, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp,
				       buffer);
		safe_unpack32(&object_ptr->flags, buffer);
		safe_unpack32(&object_ptr->id, buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->node_inx, &uint32_tmp,
				       buffer);
		safe_unpack_time(&object_ptr->time_end, buffer);
		safe_unpack_time(&object_ptr->time_start, buffer);
		safe_unpack_time(&object_ptr->time_start_prev, buffer);
		safe_unpackstr_xmalloc(&object_ptr->tres_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->tres_list =
				list_create(slurmdb_destroy_tres_rec);
			for (i=0; i<count; i++) {
				if (slurmdb_unpack_tres_rec(
					    &tmp_info, protocol_version, buffer)
				    != SLURM_SUCCESS)
					goto unpack_error;
				list_append(object_ptr->tres_list, tmp_info);
			}
		}
		safe_unpackdouble(&object_ptr->unused_wall, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&object_ptr->assocs, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&object_ptr->cluster, &uint32_tmp,
				       buffer);
		safe_unpack32(&object_ptr->flags, buffer);
		safe_unpack32(&object_ptr->id, buffer);
		safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->node_inx, &uint32_tmp,
				       buffer);
		safe_unpack_time(&object_ptr->time_end, buffer);
		safe_unpack_time(&object_ptr->time_start, buffer);
		safe_unpack_time(&object_ptr->time_start_prev, buffer);
		safe_unpackstr_xmalloc(&object_ptr->tres_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&count, buffer);
		if (count != NO_VAL) {
			object_ptr->tres_list =
				list_create(slurmdb_destroy_tres_rec);
			for (i=0; i<count; i++) {
				if (slurmdb_unpack_tres_rec(
					    &tmp_info, protocol_version, buffer)
				    != SLURM_SUCCESS)
					goto unpack_error;
				list_append(object_ptr->tres_list, tmp_info);
			}
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_reservation_rec(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}


extern void slurmdb_pack_res_rec(void *in, uint16_t protocol_version, Buf buffer)
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

	if (count && (count != NO_VAL)) {
		itr = list_iterator_create(object->clus_res_list);
		while ((clus_res = list_next(itr)))
			slurmdb_pack_clus_res_rec(
				clus_res, protocol_version, buffer);
		list_iterator_destroy(itr);
	}

	if (object->clus_res_rec) {
		pack32(0, buffer); /* anything not NO_VAL */
		slurmdb_pack_clus_res_rec(
			object->clus_res_rec, protocol_version, buffer);
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

extern int slurmdb_unpack_res_rec(void **object, uint16_t protocol_version,
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
				    (void **)&clus_res, protocol_version, buffer)
			    != SLURM_SUCCESS)
				goto unpack_error;
			list_append(object_ptr->clus_res_list, clus_res);
		}
	}

	safe_unpack32(&count, buffer);
	if (count != NO_VAL) {
		if (slurmdb_unpack_clus_res_rec(
			    (void **)&object_ptr->clus_res_rec,
			    protocol_version, buffer) != SLURM_SUCCESS)
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

extern void slurmdb_pack_txn_rec(void *in, uint16_t protocol_version, Buf buffer)
{
	slurmdb_txn_rec_t *object = (slurmdb_txn_rec_t *)in;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
	void **object, uint16_t protocol_version, Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_txn_rec_t *object_ptr = xmalloc(sizeof(slurmdb_txn_rec_t));

	*object = object_ptr;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

extern void slurmdb_pack_wckey_rec(void *in, uint16_t protocol_version,
				   Buf buffer)
{
	slurmdb_accounting_rec_t *slurmdb_info = NULL;
	ListIterator itr = NULL;
	uint32_t count = NO_VAL;
	slurmdb_wckey_rec_t *object = (slurmdb_wckey_rec_t *)in;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->accounting_list);
			while ((slurmdb_info = list_next(itr))) {
				slurmdb_pack_accounting_rec(
					slurmdb_info, protocol_version, buffer);
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

extern int slurmdb_unpack_wckey_rec(void **object, uint16_t protocol_version,
				    Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_wckey_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_wckey_rec_t));
	slurmdb_accounting_rec_t *slurmdb_info = NULL;

	*object = object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->accounting_list =
				list_create(slurmdb_destroy_accounting_rec);
			for (i = 0; i < count; i++) {
				if (slurmdb_unpack_accounting_rec(
					    (void **)&slurmdb_info,
					    protocol_version,
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

extern void slurmdb_pack_archive_rec(void *in, uint16_t protocol_version,
				     Buf buffer)
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

extern int slurmdb_unpack_archive_rec(void **object, uint16_t protocol_version,
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

extern void slurmdb_pack_tres_cond(void *in, uint16_t protocol_version,
				   Buf buffer)
{
	slurmdb_tres_cond_t *object = (slurmdb_tres_cond_t *)in;
	ListIterator itr = NULL;
	uint32_t count;
	char *tmp_info = NULL;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (!object) {
			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			return;
		}

		pack64(object->count, buffer);

		count = _list_count_null(object->format_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->format_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->id_list)
			count = list_count(object->id_list);
		else
			count = NO_VAL;
		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->name_list)
			count = list_count(object->name_list);
		else
			count = NO_VAL;
		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->name_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->type_list)
			count = list_count(object->type_list);
		else
			count = NO_VAL;
		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->type_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->with_deleted, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			pack64(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			return;
		}

		pack64(object->count, buffer);

		if (object->id_list)
			count = list_count(object->id_list);
		else
			count = NO_VAL;
		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->name_list)
			count = list_count(object->name_list);
		else
			count = NO_VAL;
		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->name_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->type_list)
			count = list_count(object->type_list);
		else
			count = NO_VAL;
		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->type_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->with_deleted, buffer);
	}
}

extern int slurmdb_unpack_tres_cond(void **object, uint16_t protocol_version,
				    Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	char *tmp_info = NULL;
	slurmdb_tres_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_tres_cond_t));

	*object = object_ptr;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {

		safe_unpack64(&object_ptr->count, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->format_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->format_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			if (!object_ptr->id_list)
				object_ptr->id_list =
					list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(
					&tmp_info, &uint32_tmp, buffer);
				list_append(object_ptr->id_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			if (!object_ptr->name_list)
				object_ptr->name_list =
					list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(
					&tmp_info, &uint32_tmp, buffer);
				list_append(object_ptr->name_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			if (!object_ptr->type_list)
				object_ptr->type_list =
					list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(
					&tmp_info, &uint32_tmp, buffer);
				list_append(object_ptr->type_list,
					    tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_deleted, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {

		safe_unpack64(&object_ptr->count, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			if (!object_ptr->id_list)
				object_ptr->id_list =
					list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(
					&tmp_info, &uint32_tmp, buffer);
				list_append(object_ptr->id_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			if (!object_ptr->name_list)
				object_ptr->name_list =
					list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(
					&tmp_info, &uint32_tmp, buffer);
				list_append(object_ptr->name_list,
					    tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			if (!object_ptr->type_list)
				object_ptr->type_list =
					list_create(slurm_destroy_char);
			for (i=0; i<count; i++) {
				safe_unpackstr_xmalloc(
					&tmp_info, &uint32_tmp, buffer);
				list_append(object_ptr->type_list,
					    tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_deleted, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_tres_cond(object_ptr);
	*object = NULL;

	return SLURM_ERROR;
}

extern void slurmdb_pack_tres_rec(void *in, uint16_t protocol_version,
				  Buf buffer)
{
	slurmdb_tres_rec_t *object = (slurmdb_tres_rec_t *)in;

	if (!object) {
		pack64(0, buffer);
		pack64(0, buffer);
		pack32(0, buffer);
		packnull(buffer);
		packnull(buffer);
		return;
	}

	pack64(object->alloc_secs, buffer);
	pack64(object->count, buffer);
	pack32(object->id, buffer);
	packstr(object->name, buffer);
	packstr(object->type, buffer);
}

extern int slurmdb_unpack_tres_rec_noalloc(
	slurmdb_tres_rec_t *object_ptr, uint16_t protocol_version, Buf buffer)
{
	uint32_t uint32_tmp;

	safe_unpack64(&object_ptr->alloc_secs, buffer);
	safe_unpack64(&object_ptr->count, buffer);
	safe_unpack32(&object_ptr->id, buffer);
	safe_unpackstr_xmalloc(&object_ptr->name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&object_ptr->type, &uint32_tmp, buffer);

	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;

}

extern int slurmdb_unpack_tres_rec(void **object, uint16_t protocol_version,
				    Buf buffer)
{
	int rc;
	slurmdb_tres_rec_t *object_ptr =
		xmalloc(sizeof(slurmdb_tres_rec_t));

	*object = object_ptr;

	rc = slurmdb_unpack_tres_rec_noalloc(object_ptr, protocol_version,
					     buffer);

	if (rc != SLURM_SUCCESS) {
		slurmdb_destroy_tres_rec(object_ptr);
		*object = NULL;
	}

	return rc;
}

extern void slurmdb_pack_user_cond(void *in, uint16_t protocol_version, Buf buffer)
{
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	slurmdb_user_cond_t *object = (slurmdb_user_cond_t *)in;
	uint32_t count = NO_VAL;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			pack16(0, buffer);
			slurmdb_pack_assoc_cond(
				NULL, protocol_version, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}

		pack16(object->admin_level, buffer);

		slurmdb_pack_assoc_cond(object->assoc_cond,
					protocol_version, buffer);

		if (object->def_acct_list)
			count = list_count(object->def_acct_list);

		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
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

		if (count && (count != NO_VAL)) {
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

extern int slurmdb_unpack_user_cond(void **object, uint16_t protocol_version,
				    Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_user_cond_t *object_ptr = xmalloc(sizeof(slurmdb_user_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&object_ptr->admin_level, buffer);

		if (slurmdb_unpack_assoc_cond(
			    (void **)&object_ptr->assoc_cond,
			    protocol_version, buffer) == SLURM_ERROR)
			goto unpack_error;

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			if (!object_ptr->def_acct_list)
				object_ptr->def_acct_list =
					list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(
					&tmp_info, &uint32_tmp, buffer);
				list_append(object_ptr->def_acct_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->def_wckey_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_user_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_account_cond(void *in, uint16_t protocol_version,
				      Buf buffer)
{
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	slurmdb_account_cond_t *object = (slurmdb_account_cond_t *)in;
	uint32_t count = NO_VAL;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			slurmdb_pack_assoc_cond(NULL, protocol_version,
						buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			pack16(0, buffer);
			return;
		}
		slurmdb_pack_assoc_cond(object->assoc_cond,
					protocol_version, buffer);

		count = NO_VAL;
		if (object->description_list)
			count = list_count(object->description_list);

		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
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

		if (count && (count != NO_VAL)) {
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

extern int slurmdb_unpack_account_cond(void **object, uint16_t protocol_version,
				       Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_account_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_account_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (slurmdb_unpack_assoc_cond(
			    (void **)&object_ptr->assoc_cond,
			    protocol_version, buffer) == SLURM_ERROR)
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

extern void slurmdb_pack_cluster_cond(void *in, uint16_t protocol_version,
				      Buf buffer)
{
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	slurmdb_cluster_cond_t *object = (slurmdb_cluster_cond_t *)in;
	uint32_t count = NO_VAL;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (!object) {
			pack16(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
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

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->federation_list)
			count = list_count(object->federation_list);
		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->federation_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack32(object->flags, buffer);

		count = _list_count_null(object->format_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->format_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->plugin_id_select_list)
			count = list_count(object->plugin_id_select_list);

		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
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

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(
				object->rpc_version_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack_time(object->usage_end, buffer);
		pack_time(object->usage_start, buffer);

		pack16(object->with_usage, buffer);
		pack16(object->with_deleted, buffer);
	} else if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
		if (!object) {
			pack16(0, buffer);
			pack32(NO_VAL, buffer);
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

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->federation_list)
			count = list_count(object->federation_list);
		pack32(count, buffer);

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->federation_list);
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

		if (count && (count != NO_VAL)) {
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

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(
				object->rpc_version_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack_time(object->usage_end, buffer);
		pack_time(object->usage_start, buffer);

		pack16(object->with_usage, buffer);
		pack16(object->with_deleted, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

		if (count && (count != NO_VAL)) {
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

		if (count && (count != NO_VAL)) {
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

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(
				object->rpc_version_list);
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

extern int slurmdb_unpack_cluster_cond(void **object, uint16_t protocol_version,
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
	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack16(&object_ptr->classification, buffer);
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->federation_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->federation_list,
					    tmp_info);
			}
		}

		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->format_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->format_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->plugin_id_select_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->plugin_id_select_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->rpc_version_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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
	} else if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
		safe_unpack16(&object_ptr->classification, buffer);
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->federation_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->federation_list,
					    tmp_info);
			}
		}

		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->plugin_id_select_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->plugin_id_select_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->rpc_version_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&object_ptr->classification, buffer);
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->plugin_id_select_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->plugin_id_select_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->rpc_version_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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

extern void slurmdb_pack_federation_cond(void *in, uint16_t protocol_version,
					 Buf buffer)
{
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	slurmdb_federation_cond_t *object = (slurmdb_federation_cond_t *)in;
	uint32_t count = NO_VAL;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			return;
		}

		/* cluster_list */
		if (object->cluster_list)
			count = list_count(object->cluster_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		/* federation_list */
		if (object->federation_list)
			count = list_count(object->federation_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->federation_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->format_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->format_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->with_deleted, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			return;
		}

		/* cluster_list */
		if (object->cluster_list)
			count = list_count(object->cluster_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		/* federation_list */
		if (object->federation_list)
			count = list_count(object->federation_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->federation_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->with_deleted, buffer);
	}
}

extern int slurmdb_unpack_federation_cond(void **object,
					  uint16_t protocol_version,
					  Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_federation_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_federation_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	slurmdb_init_federation_cond(object_ptr, 0);
	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->federation_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->federation_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->format_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->format_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_deleted, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->federation_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->federation_list,
					    tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_deleted, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_federation_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_assoc_cond(void *in, uint16_t protocol_version,
				    Buf buffer)
{
	char *tmp_info = NULL;
	uint32_t count = NO_VAL;

	ListIterator itr = NULL;
	slurmdb_assoc_cond_t *object = (slurmdb_assoc_cond_t *)in;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (!object) {
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
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->acct_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->cluster_list)
			count = list_count(object->cluster_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->def_qos_id_list)
			count = list_count(object->def_qos_id_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->def_qos_id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->format_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->format_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->id_list)
			count = list_count(object->id_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}

		pack16(object->only_defs, buffer);

		if (object->partition_list)
			count = list_count(object->partition_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->partition_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->parent_acct_list)
			count = list_count(object->parent_acct_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->parent_acct_list);
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

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->qos_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack_time(object->usage_end, buffer);
		pack_time(object->usage_start, buffer);

		if (object->user_list)
			count = list_count(object->user_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
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
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
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
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->acct_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->cluster_list)
			count = list_count(object->cluster_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->def_qos_id_list)
			count = list_count(object->def_qos_id_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->def_qos_id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}


		if (object->id_list)
			count = list_count(object->id_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}

		pack16(object->only_defs, buffer);

		if (object->partition_list)
			count = list_count(object->partition_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->partition_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->parent_acct_list)
			count = list_count(object->parent_acct_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->parent_acct_list);
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

		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->qos_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack_time(object->usage_end, buffer);
		pack_time(object->usage_start, buffer);

		if (object->user_list)
			count = list_count(object->user_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
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
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_assoc_cond(void **object,
				     uint16_t protocol_version, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_assoc_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_assoc_cond_t));
	char *tmp_info = NULL;
	*object = object_ptr;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->acct_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->def_qos_id_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->def_qos_id_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->format_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->format_list, tmp_info);
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

		safe_unpack16(&object_ptr->only_defs, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->partition_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->partition_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->parent_acct_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->parent_acct_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->qos_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->qos_list, tmp_info);
			}
		}

		safe_unpack_time(&object_ptr->usage_end, buffer);
		safe_unpack_time(&object_ptr->usage_start, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->user_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->acct_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->def_qos_id_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->def_qos_id_list,
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

		safe_unpack16(&object_ptr->only_defs, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->partition_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->partition_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->parent_acct_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->parent_acct_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->qos_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->qos_list, tmp_info);
			}
		}

		safe_unpack_time(&object_ptr->usage_end, buffer);
		safe_unpack_time(&object_ptr->usage_start, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->user_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_assoc_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_event_cond(void *in, uint16_t protocol_version,
				    Buf buffer)
{
	char *tmp_info = NULL;
	uint32_t count = NO_VAL;

	ListIterator itr = NULL;
	slurmdb_event_cond_t *object = (slurmdb_event_cond_t *)in;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack32(object->cpus_max, buffer);
		pack32(object->cpus_min, buffer);
		pack16(object->event_type, buffer);

		count = _list_count_null(object->format_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->format_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->node_list)
			count = list_count(object->node_list);

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->state_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack32(NO_VAL, buffer);
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->state_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
	}
}

extern int slurmdb_unpack_event_cond(void **object, uint16_t protocol_version,
				     Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_event_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_event_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->cluster_list = list_create(
				slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}
		safe_unpack32(&object_ptr->cpus_max, buffer);
		safe_unpack32(&object_ptr->cpus_min, buffer);
		safe_unpack16(&object_ptr->event_type, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->format_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->format_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->node_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->node_list, tmp_info);
			}
		}

		safe_unpack_time(&object_ptr->period_end, buffer);
		safe_unpack_time(&object_ptr->period_start, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->reason_list = list_create(
				slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->reason_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->reason_uid_list = list_create(
				slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->reason_uid_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->state_list = list_create(
				slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->state_list, tmp_info);
			}
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->cluster_list = list_create(
				slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}
		safe_unpack32(&object_ptr->cpus_max, buffer);
		safe_unpack32(&object_ptr->cpus_min, buffer);
		safe_unpack16(&object_ptr->event_type, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->node_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->node_list, tmp_info);
			}
		}

		safe_unpack_time(&object_ptr->period_end, buffer);
		safe_unpack_time(&object_ptr->period_start, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->reason_list = list_create(
				slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->reason_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->reason_uid_list = list_create(
				slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->reason_uid_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->state_list = list_create(
				slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->state_list, tmp_info);
			}
		}
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_event_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_job_cond(void *in, uint16_t protocol_version,
				  Buf buffer)
{
	char *tmp_info = NULL;
	slurmdb_selected_step_t *job = NULL;
	uint32_t count = NO_VAL;

	ListIterator itr = NULL;
	slurmdb_job_cond_t *object = (slurmdb_job_cond_t *)in;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);	/* count(acct_list) */
			pack32(NO_VAL, buffer);	/* count(associd_list) */
			pack32(NO_VAL, buffer);	/* count(cluster_list) */
			pack32(0, buffer);	/* cpus_max */
			pack32(0, buffer);	/* cpus_min */
			pack16(0, buffer);	/* duplicates */
			pack32(0, buffer);	/* exitcode */
			pack32(NO_VAL, buffer);	/* count(format_list) */
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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

		count = _list_count_null(object->format_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->format_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->groupid_list)
			count = list_count(object->groupid_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->step_list);
			while ((job = list_next(itr))) {
				slurmdb_pack_selected_step(job,
							   protocol_version,
							   buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->state_list)
			count = list_count(object->state_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->wckey_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->without_steps, buffer);
		pack16(object->without_usage_truncation, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->step_list);
			while ((job = list_next(itr))) {
				slurmdb_pack_selected_step(job,
							   protocol_version,
							   buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->state_list)
			count = list_count(object->state_list);
		else
			count = NO_VAL;
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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

extern int slurmdb_unpack_job_cond(void **object, uint16_t protocol_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_job_cond_t *object_ptr = xmalloc(sizeof(slurmdb_job_cond_t));
	char *tmp_info = NULL;
	slurmdb_selected_step_t *job = NULL;

	*object = object_ptr;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->acct_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->associd_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->associd_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->format_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->format_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->groupid_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->groupid_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->jobname_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->jobname_list, tmp_info);
			}
		}

		safe_unpack32(&object_ptr->nodes_max, buffer);
		safe_unpack32(&object_ptr->nodes_min, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->partition_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->partition_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->qos_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->resv_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->resvid_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->resvid_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->step_list =
				list_create(slurmdb_destroy_selected_step);
			for (i = 0; i < count; i++) {
				if (slurmdb_unpack_selected_step(
					&job, protocol_version, buffer)
				    != SLURM_SUCCESS) {
					error("unpacking selected step");
					goto unpack_error;
				}
				/* There is no such thing as jobid 0,
				 * if we process it the database will
				 * return all jobs. */
				if (!job->jobid)
					slurmdb_destroy_selected_step(job);
				else
					list_append(object_ptr->step_list, job);
			}
			if (!list_count(object_ptr->step_list))
				FREE_NULL_LIST(object_ptr->step_list);
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->state_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->userid_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->userid_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->wckey_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->wckey_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->without_steps, buffer);
		safe_unpack16(&object_ptr->without_usage_truncation, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->acct_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->associd_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->associd_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->groupid_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->groupid_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->jobname_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->jobname_list, tmp_info);
			}
		}

		safe_unpack32(&object_ptr->nodes_max, buffer);
		safe_unpack32(&object_ptr->nodes_min, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->partition_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->partition_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->qos_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->resv_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->resvid_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->resvid_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->step_list =
				list_create(slurmdb_destroy_selected_step);
			for (i = 0; i < count; i++) {
				if (slurmdb_unpack_selected_step(
					&job, protocol_version, buffer)
				    != SLURM_SUCCESS) {
					error("unpacking selected step");
					goto unpack_error;
				}
				/* There is no such thing as jobid 0,
				 * if we process it the database will
				 * return all jobs. */
				if (!job->jobid)
					slurmdb_destroy_selected_step(job);
				else
					list_append(object_ptr->step_list, job);
			}
			if (!list_count(object_ptr->step_list))
				FREE_NULL_LIST(object_ptr->step_list);
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->state_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->userid_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->userid_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->wckey_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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

extern void slurmdb_pack_job_modify_cond(void *in, uint16_t protocol_version,
					 Buf buffer)
{
	slurmdb_job_modify_cond_t *cond = (slurmdb_job_modify_cond_t *)in;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!cond) {
			packnull(buffer);
			pack32(NO_VAL, buffer);
			return;
		}
		packstr(cond->cluster, buffer);
		pack32(cond->job_id, buffer);
	}
}

extern int slurmdb_unpack_job_modify_cond(void **object,
					  uint16_t protocol_version,
					  Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_job_modify_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_job_modify_cond_t));

	*object = object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

extern void slurmdb_pack_job_rec(void *object, uint16_t protocol_version,
				 Buf buffer)
{
	slurmdb_job_rec_t *job = (slurmdb_job_rec_t *)object;
	ListIterator itr = NULL;
	slurmdb_step_rec_t *step = NULL;
	uint32_t count = 0;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		packstr(job->account, buffer);
		packstr(job->admin_comment, buffer);
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
		packstr(job->mcs_label, buffer);
		packstr(job->nodes, buffer);
		pack32(job->pack_job_id, buffer);
		pack32(job->pack_job_offset, buffer);
		packstr(job->partition, buffer);
		pack32(job->priority, buffer);
		pack32(job->qosid, buffer);
		pack32(job->req_cpus, buffer);
		packstr(job->req_gres, buffer);
		pack64(job->req_mem, buffer);
		pack32(job->requid, buffer);
		packstr(job->resv_name, buffer);
		pack32(job->resvid, buffer);
		pack32(job->show_full, buffer);
		pack_time(job->start, buffer);
		pack32(job->state, buffer);
		_pack_slurmdb_stats(&job->stats, protocol_version, buffer);

		if (job->steps)
			count = list_count(job->steps);
		else
			count = 0;

		pack32(count, buffer);
		if (count) {
			itr = list_iterator_create(job->steps);
			while ((step = list_next(itr))) {
				slurmdb_pack_step_rec(step, protocol_version,
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

		packstr(job->tres_alloc_str, buffer);
		packstr(job->tres_req_str, buffer);

		pack32(job->uid, buffer);
		packstr(job->user, buffer);
		pack32(job->user_cpu_sec, buffer);
		pack32(job->user_cpu_usec, buffer);
		packstr(job->wckey, buffer);
		pack32(job->wckeyid, buffer);
		packstr(job->work_dir, buffer);
	} else if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
		packstr(job->account, buffer);
		packstr(job->admin_comment, buffer);
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
		pack64(job->req_mem, buffer);
		pack32(job->requid, buffer);
		packstr(job->resv_name, buffer);
		pack32(job->resvid, buffer);
		pack32(job->show_full, buffer);
		pack_time(job->start, buffer);
		pack32(job->state, buffer);
		_pack_slurmdb_stats(&job->stats, protocol_version, buffer);

		if (job->steps)
			count = list_count(job->steps);
		else
			count = 0;

		pack32(count, buffer);
		if (count) {
			itr = list_iterator_create(job->steps);
			while ((step = list_next(itr))) {
				slurmdb_pack_step_rec(step, protocol_version,
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

		packstr(job->tres_alloc_str, buffer);
		packstr(job->tres_req_str, buffer);

		pack32(job->uid, buffer);
		packstr(job->user, buffer);
		pack32(job->user_cpu_sec, buffer);
		pack32(job->user_cpu_usec, buffer);
		packstr(job->wckey, buffer);
		pack32(job->wckeyid, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(job->account, buffer);
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
		/* first_step_ptr is set on the client side, not packed */
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
		pack32(xlate_mem_new2old(job->req_mem), buffer);
		pack32(job->requid, buffer);
		packstr(job->resv_name, buffer);
		pack32(job->resvid, buffer);
		pack32(job->show_full, buffer);
		pack_time(job->start, buffer);
		pack32(job->state, buffer);
		_pack_slurmdb_stats(&job->stats, protocol_version, buffer);

		if (job->steps)
			count = list_count(job->steps);
		else
			count = 0;

		pack32(count, buffer);
		if (count) {
			itr = list_iterator_create(job->steps);
			while ((step = list_next(itr))) {
				slurmdb_pack_step_rec(step, protocol_version,
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

		packstr(job->tres_alloc_str, buffer);
		packstr(job->tres_req_str, buffer);

		pack32(job->uid, buffer);
		packstr(job->user, buffer);
		pack32(job->user_cpu_sec, buffer);
		pack32(job->user_cpu_usec, buffer);
		packstr(job->wckey, buffer); /* added for protocol_version 4 */
		pack32(job->wckeyid, buffer); /* added for protocol_version 4 */
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_job_rec(void **job, uint16_t protocol_version,
				  Buf buffer)
{
	slurmdb_job_rec_t *job_ptr = xmalloc(sizeof(slurmdb_job_rec_t));
	int i = 0;
	slurmdb_step_rec_t *step = NULL;
	uint32_t count = 0;
	uint32_t uint32_tmp;

	*job = job_ptr;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&job_ptr->account, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_ptr->admin_comment, &uint32_tmp,
				       buffer);
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
		safe_unpackstr_xmalloc(&job_ptr->mcs_label,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_ptr->nodes, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->pack_job_id, buffer);
		safe_unpack32(&job_ptr->pack_job_offset, buffer);
		safe_unpackstr_xmalloc(&job_ptr->partition, &uint32_tmp,
				       buffer);
		safe_unpack32(&job_ptr->priority, buffer);
		safe_unpack32(&job_ptr->qosid, buffer);
		safe_unpack32(&job_ptr->req_cpus, buffer);
		safe_unpackstr_xmalloc(&job_ptr->req_gres, &uint32_tmp, buffer);
		safe_unpack64(&job_ptr->req_mem, buffer);
		safe_unpack32(&job_ptr->requid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->resv_name, &uint32_tmp,
				       buffer);
		safe_unpack32(&job_ptr->resvid, buffer);
		safe_unpack32(&job_ptr->show_full, buffer);
		safe_unpack_time(&job_ptr->start, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		job_ptr->state = uint32_tmp;
		if (_unpack_slurmdb_stats(&job_ptr->stats, protocol_version,
					  buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack32(&count, buffer);
		job_ptr->steps = list_create(slurmdb_destroy_step_rec);
		for (i = 0; i < count; i++) {
			if (slurmdb_unpack_step_rec(&step, protocol_version,
						    buffer)
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
		safe_unpackstr_xmalloc(&job_ptr->tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_ptr->tres_req_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->uid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->user, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->user_cpu_sec, buffer);
		safe_unpack32(&job_ptr->user_cpu_usec, buffer);
		safe_unpackstr_xmalloc(&job_ptr->wckey, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->wckeyid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->work_dir, &uint32_tmp, buffer);
	} else if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&job_ptr->account, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_ptr->admin_comment, &uint32_tmp,
				       buffer);
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
		safe_unpack64(&job_ptr->req_mem, buffer);
		safe_unpack32(&job_ptr->requid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->resv_name, &uint32_tmp,
				       buffer);
		safe_unpack32(&job_ptr->resvid, buffer);
		safe_unpack32(&job_ptr->show_full, buffer);
		safe_unpack_time(&job_ptr->start, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		job_ptr->state = uint32_tmp;
		if (_unpack_slurmdb_stats(&job_ptr->stats, protocol_version,
					  buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack32(&count, buffer);
		job_ptr->steps = list_create(slurmdb_destroy_step_rec);
		for (i=0; i<count; i++) {
			if (slurmdb_unpack_step_rec(&step, protocol_version,
						    buffer)
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
		safe_unpackstr_xmalloc(&job_ptr->tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_ptr->tres_req_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->uid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->user, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->user_cpu_sec, buffer);
		safe_unpack32(&job_ptr->user_cpu_usec, buffer);
		safe_unpackstr_xmalloc(&job_ptr->wckey, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->wckeyid, buffer);
	} else  if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint32_t tmp_mem;
		safe_unpackstr_xmalloc(&job_ptr->account, &uint32_tmp, buffer);
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
		safe_unpack32(&job_ptr->derived_ec, buffer);
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
		safe_unpack32(&tmp_mem, buffer);
		job_ptr->req_mem = xlate_mem_old2new(tmp_mem);
		safe_unpack32(&job_ptr->requid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->resv_name, &uint32_tmp,
				       buffer);
		safe_unpack32(&job_ptr->resvid, buffer);
		safe_unpack32(&job_ptr->show_full, buffer);
		safe_unpack_time(&job_ptr->start, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		job_ptr->state = uint32_tmp;
		if (_unpack_slurmdb_stats(&job_ptr->stats, protocol_version,
					  buffer)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack32(&count, buffer);
		job_ptr->steps = list_create(slurmdb_destroy_step_rec);
		for (i=0; i<count; i++) {
			if (slurmdb_unpack_step_rec(&step, protocol_version,
						    buffer)
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
		safe_unpackstr_xmalloc(&job_ptr->tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&job_ptr->tres_req_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->uid, buffer);
		safe_unpackstr_xmalloc(&job_ptr->user, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->user_cpu_sec, buffer);
		safe_unpack32(&job_ptr->user_cpu_usec, buffer);
		safe_unpackstr_xmalloc(&job_ptr->wckey, &uint32_tmp, buffer);
		safe_unpack32(&job_ptr->wckeyid, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_job_rec(job_ptr);
	*job = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_qos_cond(void *in, uint16_t protocol_version,
				  Buf buffer)
{
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	slurmdb_qos_cond_t *object = (slurmdb_qos_cond_t *)in;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->description_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->format_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->format_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->id_list)
			count = list_count(object->id_list);

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->name_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->preempt_mode, buffer);
		pack16(object->with_deleted, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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

extern int slurmdb_unpack_qos_cond(void **object, uint16_t protocol_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_qos_cond_t *object_ptr = xmalloc(sizeof(slurmdb_qos_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->description_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->description_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->format_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->format_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->name_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->preempt_mode, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->description_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->description_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->name_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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

extern void slurmdb_pack_reservation_cond(void *in, uint16_t protocol_version,
					  Buf buffer)
{
	slurmdb_reservation_cond_t *object = (slurmdb_reservation_cond_t *)in;
	uint32_t count = NO_VAL;
	ListIterator itr = NULL;
	char *tmp_info = NULL;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack32(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			packnull(buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			pack16(0, buffer);
			return;
		}

		if (object->cluster_list)
			count = list_count(object->cluster_list);

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack32(object->flags, buffer);

		count = _list_count_null(object->format_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->format_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		if (object->id_list)
			count = list_count(object->id_list);
		else
			count = NO_VAL;

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			packnull(buffer);
			pack_time(0, buffer);
			pack_time(0, buffer);
			pack16(0, buffer);
			return;
		}

		if (object->cluster_list)
			count = list_count(object->cluster_list);

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		pack16((uint16_t)object->flags, buffer);

		if (object->id_list)
			count = list_count(object->id_list);

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
}

extern int slurmdb_unpack_reservation_cond(void **object,
					   uint16_t protocol_version,
					   Buf buffer)
{
	uint32_t uint32_tmp, count;
	int i = 0;
	char *tmp_info = NULL;
	slurmdb_reservation_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_reservation_cond_t));

	*object = object_ptr;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->cluster_list = list_create(
				slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->format_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->format_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->name_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpackstr_xmalloc(&object_ptr->nodes, &uint32_tmp, buffer);
		safe_unpack_time(&object_ptr->time_end, buffer);
		safe_unpack_time(&object_ptr->time_start, buffer);
		safe_unpack16(&object_ptr->with_usage, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint16_t tmp16;
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->cluster_list = list_create(
				slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack16(&tmp16, buffer);
		object_ptr->flags = tmp16;

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->name_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpackstr_xmalloc(&object_ptr->nodes, &uint32_tmp, buffer);
		safe_unpack_time(&object_ptr->time_end, buffer);
		safe_unpack_time(&object_ptr->time_start, buffer);
		safe_unpack16(&object_ptr->with_usage, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_reservation_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_selected_step(slurmdb_selected_step_t *step,
				       uint16_t protocol_version, Buf buffer)
{
	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		pack32(step->array_task_id, buffer);
		pack32(step->jobid, buffer);
		pack32(step->pack_job_offset, buffer);
		pack32(step->stepid, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(step->array_task_id, buffer);
		pack32(step->jobid, buffer);
		pack32(step->stepid, buffer);
	}
}

extern int slurmdb_unpack_selected_step(slurmdb_selected_step_t **step,
					uint16_t protocol_version, Buf buffer)
{
	slurmdb_selected_step_t *step_ptr =
		xmalloc(sizeof(slurmdb_selected_step_t));

	*step = step_ptr;

	step_ptr->array_task_id = NO_VAL;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack32(&step_ptr->array_task_id, buffer);
		safe_unpack32(&step_ptr->jobid, buffer);
		safe_unpack32(&step_ptr->pack_job_offset, buffer);
		safe_unpack32(&step_ptr->stepid, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&step_ptr->array_task_id, buffer);
		safe_unpack32(&step_ptr->jobid, buffer);
		safe_unpack32(&step_ptr->stepid, buffer);
		step_ptr->pack_job_offset = NO_VAL;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_selected_step(step_ptr);
	*step = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_step_rec(slurmdb_step_rec_t *step,
				  uint16_t protocol_version, Buf buffer)
{

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		pack32(step->elapsed, buffer);
		pack_time(step->end, buffer);
		pack32((uint32_t)step->exitcode, buffer);
		pack32(step->nnodes, buffer);
		packstr(step->nodes, buffer);
		pack32(step->ntasks, buffer);
		pack32(step->req_cpufreq_min, buffer);
		pack32(step->req_cpufreq_max, buffer);
		pack32(step->req_cpufreq_gov, buffer);
		pack32(step->requid, buffer);
		_pack_slurmdb_stats(&step->stats, protocol_version, buffer);
		pack_time(step->start, buffer);
		pack16(step->state, buffer);
		pack32(step->stepid, buffer);   /* job's step number */
		packstr(step->stepname, buffer);
		pack32(step->suspended, buffer);
		pack32(step->sys_cpu_sec, buffer);
		pack32(step->sys_cpu_usec, buffer);
		pack32(step->task_dist, buffer);
		pack32(step->tot_cpu_sec, buffer);
		pack32(step->tot_cpu_usec, buffer);
		packstr(step->tres_alloc_str, buffer);
		pack32(step->user_cpu_sec, buffer);
		pack32(step->user_cpu_usec, buffer);
	} else if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
		pack32(step->elapsed, buffer);
		pack_time(step->end, buffer);
		pack32((uint32_t)step->exitcode, buffer);
		pack32(step->nnodes, buffer);
		packstr(step->nodes, buffer);
		pack32(step->ntasks, buffer);
		pack32((uint32_t) 0, buffer);
		pack32((uint32_t) 0, buffer);
		pack32(step->req_cpufreq_min, buffer);
		pack32(step->req_cpufreq_max, buffer);
		pack32(step->req_cpufreq_gov, buffer);
		pack32(step->requid, buffer);
		_pack_slurmdb_stats(&step->stats, protocol_version, buffer);
		pack_time(step->start, buffer);
		pack16(step->state, buffer);
		pack32(step->stepid, buffer);   /* job's step number */
		packstr(step->stepname, buffer);
		pack32(step->suspended, buffer);
		pack32(step->sys_cpu_sec, buffer);
		pack32(step->sys_cpu_usec, buffer);
		pack32(step->task_dist, buffer);
		pack32(step->tot_cpu_sec, buffer);
		pack32(step->tot_cpu_usec, buffer);
		packstr(step->tres_alloc_str, buffer);
		pack32(step->user_cpu_sec, buffer);
		pack32(step->user_cpu_usec, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(step->elapsed, buffer);
		pack_time(step->end, buffer);
		pack32((uint32_t)step->exitcode, buffer);
		pack32(step->nnodes, buffer);
		packstr(step->nodes, buffer);
		pack32(step->ntasks, buffer);
		pack32(step->req_cpufreq_min, buffer);
		pack32(step->req_cpufreq_max, buffer);
		pack32(step->req_cpufreq_gov, buffer);
		pack32(step->requid, buffer);
		_pack_slurmdb_stats(&step->stats, protocol_version, buffer);
		pack_time(step->start, buffer);
		pack16(step->state, buffer);
		pack32(step->stepid, buffer);	/* job's step number */
		packstr(step->stepname, buffer);
		pack32(step->suspended, buffer);
		pack32(step->sys_cpu_sec, buffer);
		pack32(step->sys_cpu_usec, buffer);
		pack32(step->task_dist, buffer);
		pack32(step->tot_cpu_sec, buffer);
		pack32(step->tot_cpu_usec, buffer);
		packstr(step->tres_alloc_str, buffer);
		pack32(step->user_cpu_sec, buffer);
		pack32(step->user_cpu_usec, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_step_rec(slurmdb_step_rec_t **step,
				   uint16_t protocol_version, Buf buffer)
{
	uint32_t uint32_tmp = 0;
	uint16_t uint16_tmp = 0;
	slurmdb_step_rec_t *step_ptr = xmalloc(sizeof(slurmdb_step_rec_t));

	*step = step_ptr;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack32(&step_ptr->elapsed, buffer);
		safe_unpack_time(&step_ptr->end, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		step_ptr->exitcode = (int32_t)uint32_tmp;
		safe_unpack32(&step_ptr->nnodes, buffer);
		safe_unpackstr_xmalloc(&step_ptr->nodes, &uint32_tmp, buffer);
		safe_unpack32(&step_ptr->ntasks, buffer);
		safe_unpack32(&step_ptr->req_cpufreq_min, buffer);
		safe_unpack32(&step_ptr->req_cpufreq_max, buffer);
		safe_unpack32(&step_ptr->req_cpufreq_gov, buffer);
		safe_unpack32(&step_ptr->requid, buffer);
		if (_unpack_slurmdb_stats(&step_ptr->stats, protocol_version,
					  buffer)
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
		safe_unpack32(&step_ptr->task_dist, buffer);
		safe_unpack32(&step_ptr->tot_cpu_sec, buffer);
		safe_unpack32(&step_ptr->tot_cpu_usec, buffer);
		safe_unpackstr_xmalloc(&step_ptr->tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&step_ptr->user_cpu_sec, buffer);
		safe_unpack32(&step_ptr->user_cpu_usec, buffer);
	} else if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
		safe_unpack32(&step_ptr->elapsed, buffer);
		safe_unpack_time(&step_ptr->end, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		step_ptr->exitcode = (int32_t)uint32_tmp;
		safe_unpack32(&step_ptr->nnodes, buffer);
		safe_unpackstr_xmalloc(&step_ptr->nodes, &uint32_tmp, buffer);
		safe_unpack32(&step_ptr->ntasks, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		safe_unpack32(&step_ptr->req_cpufreq_min, buffer);
		safe_unpack32(&step_ptr->req_cpufreq_max, buffer);
		safe_unpack32(&step_ptr->req_cpufreq_gov, buffer);
		safe_unpack32(&step_ptr->requid, buffer);
		if (_unpack_slurmdb_stats(&step_ptr->stats, protocol_version,
					  buffer)
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
		safe_unpack32(&step_ptr->task_dist, buffer);
		safe_unpack32(&step_ptr->tot_cpu_sec, buffer);
		safe_unpack32(&step_ptr->tot_cpu_usec, buffer);
		safe_unpackstr_xmalloc(&step_ptr->tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&step_ptr->user_cpu_sec, buffer);
		safe_unpack32(&step_ptr->user_cpu_usec, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&step_ptr->elapsed, buffer);
		safe_unpack_time(&step_ptr->end, buffer);
		safe_unpack32(&uint32_tmp, buffer);
		step_ptr->exitcode = (int32_t)uint32_tmp;
		safe_unpack32(&step_ptr->nnodes, buffer);
		safe_unpackstr_xmalloc(&step_ptr->nodes, &uint32_tmp, buffer);
		safe_unpack32(&step_ptr->ntasks, buffer);
		safe_unpack32(&step_ptr->req_cpufreq_min, buffer);
		safe_unpack32(&step_ptr->req_cpufreq_max, buffer);
		safe_unpack32(&step_ptr->req_cpufreq_gov, buffer);
		safe_unpack32(&step_ptr->requid, buffer);
		if (_unpack_slurmdb_stats(&step_ptr->stats, protocol_version,
					  buffer)
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
		safe_unpack32(&step_ptr->task_dist, buffer);
		safe_unpack32(&step_ptr->tot_cpu_sec, buffer);
		safe_unpack32(&step_ptr->tot_cpu_usec, buffer);
		safe_unpackstr_xmalloc(&step_ptr->tres_alloc_str,
				       &uint32_tmp, buffer);
		safe_unpack32(&step_ptr->user_cpu_sec, buffer);
		safe_unpack32(&step_ptr->user_cpu_usec, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_step_rec(step_ptr);
	*step = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_res_cond(void *in, uint16_t protocol_version,
				  Buf buffer)
{
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	slurmdb_res_cond_t *object = (slurmdb_res_cond_t *)in;
	uint32_t count;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
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
			pack32(NO_VAL, buffer);
			pack16(0, buffer);
			pack16(0, buffer);

			return;
		}

		count = _list_count_null(object->cluster_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->description_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->description_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack32(object->flags, buffer);

		count = _list_count_null(object->format_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->format_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->id_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->manager_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->manager_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->name_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->name_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->percent_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->percent_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->server_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->server_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->type_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->type_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->with_deleted, buffer);
		pack16(object->with_clusters, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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

		count = _list_count_null(object->cluster_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->description_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->description_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack32(object->flags, buffer);

		count = _list_count_null(object->id_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->manager_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->manager_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->name_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->name_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->percent_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->percent_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->server_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->server_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->type_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->type_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->with_deleted, buffer);
		pack16(object->with_clusters, buffer);
	}
}

extern int slurmdb_unpack_res_cond(void **object, uint16_t protocol_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count = 0;
	slurmdb_res_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_res_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	slurmdb_init_res_cond(object_ptr, 0);

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->description_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->description_list,
					    tmp_info);
			}
		}

		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->format_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->format_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->id_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->manager_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->manager_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->name_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->percent_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->percent_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->server_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->server_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->type_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->type_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_deleted, buffer);
		safe_unpack16(&object_ptr->with_clusters, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->description_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->description_list,
					    tmp_info);
			}
		}

		safe_unpack32(&object_ptr->flags, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->id_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->manager_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->manager_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->name_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->percent_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->percent_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->server_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->server_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->type_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->type_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_deleted, buffer);
		safe_unpack16(&object_ptr->with_clusters, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_res_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_txn_cond(void *in, uint16_t protocol_version,
				  Buf buffer)
{
	uint32_t count = NO_VAL;
	char *tmp_info = NULL;
	ListIterator itr = NULL;
	slurmdb_txn_cond_t *object = (slurmdb_txn_cond_t *)in;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->format_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->format_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->id_list)
			count = list_count(object->id_list);

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->user_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->with_assoc_info, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->user_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->with_assoc_info, buffer);
	}
}

extern int slurmdb_unpack_txn_cond(void **object, uint16_t protocol_version,
				   Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_txn_cond_t *object_ptr = xmalloc(sizeof(slurmdb_txn_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;
	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->acct_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->action_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->action_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->actor_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->actor_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->format_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->format_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->info_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->info_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->name_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpack_time(&object_ptr->time_end, buffer);
		safe_unpack_time(&object_ptr->time_start, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->user_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->user_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_assoc_info, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->acct_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->acct_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->action_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->action_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->actor_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->actor_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->cluster_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->info_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->info_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->name_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info,
						       &uint32_tmp, buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpack_time(&object_ptr->time_end, buffer);
		safe_unpack_time(&object_ptr->time_start, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->user_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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

extern void slurmdb_pack_wckey_cond(void *in, uint16_t protocol_version,
				    Buf buffer)
{
	char *tmp_info = NULL;
	uint32_t count = NO_VAL;

	ListIterator itr = NULL;
	slurmdb_wckey_cond_t *object = (slurmdb_wckey_cond_t *)in;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		if (!object) {
			pack32(NO_VAL, buffer);
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->cluster_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		count = _list_count_null(object->format_list);
		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->format_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}
		count = NO_VAL;

		if (object->id_list)
			count = list_count(object->id_list);

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;

		if (object->name_list)
			count = list_count(object->name_list);

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->user_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
			list_iterator_destroy(itr);
		}

		pack16(object->with_usage, buffer);
		pack16(object->with_deleted, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
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
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
			itr = list_iterator_create(object->id_list);
			while ((tmp_info = list_next(itr))) {
				packstr(tmp_info, buffer);
			}
		}
		count = NO_VAL;

		if (object->name_list)
			count = list_count(object->name_list);

		pack32(count, buffer);
		if (count && (count != NO_VAL)) {
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
		if (count && (count != NO_VAL)) {
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

extern int slurmdb_unpack_wckey_cond(void **object, uint16_t protocol_version,
				     Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	uint32_t count;
	slurmdb_wckey_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_wckey_cond_t));
	char *tmp_info = NULL;

	*object = object_ptr;

	if (protocol_version >= SLURM_17_11_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count && (count != NO_VAL)) {
			object_ptr->format_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->format_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->name_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->only_defs, buffer);

		safe_unpack_time(&object_ptr->usage_end, buffer);
		safe_unpack_time(&object_ptr->usage_start, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->user_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->user_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->with_usage, buffer);
		safe_unpack16(&object_ptr->with_deleted, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->cluster_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->cluster_list,
					    tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->id_list = list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->id_list, tmp_info);
			}
		}

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->name_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
				safe_unpackstr_xmalloc(&tmp_info, &uint32_tmp,
						       buffer);
				list_append(object_ptr->name_list, tmp_info);
			}
		}

		safe_unpack16(&object_ptr->only_defs, buffer);

		safe_unpack_time(&object_ptr->usage_end, buffer);
		safe_unpack_time(&object_ptr->usage_start, buffer);

		safe_unpack32(&count, buffer);
		if (count > NO_VAL)
			goto unpack_error;
		if (count != NO_VAL) {
			object_ptr->user_list =
				list_create(slurm_destroy_char);
			for (i = 0; i < count; i++) {
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

extern void slurmdb_pack_archive_cond(void *in, uint16_t protocol_version,
				      Buf buffer)
{
	slurmdb_archive_cond_t *object = (slurmdb_archive_cond_t *)in;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!object) {
			packnull(buffer);
			packnull(buffer);
			slurmdb_pack_job_cond(NULL, protocol_version, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			pack32(NO_VAL, buffer);
			return;
		}

		packstr(object->archive_dir, buffer);
		packstr(object->archive_script, buffer);
		slurmdb_pack_job_cond(object->job_cond,
				      protocol_version, buffer);
		pack32(object->purge_event, buffer);
		pack32(object->purge_job, buffer);
		pack32(object->purge_resv, buffer);
		pack32(object->purge_step, buffer);
		pack32(object->purge_suspend, buffer);
	}
}

extern int slurmdb_unpack_archive_cond(void **object, uint16_t protocol_version,
				       Buf buffer)
{
	uint32_t uint32_tmp;
	slurmdb_archive_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_archive_cond_t));

	*object = object_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/*
		 * Looks like we missed these when added to the structure.
		 * Correctly fixed in 18.08.
		 */
		object_ptr->purge_txn = NO_VAL;
		object_ptr->purge_usage = NO_VAL;

		safe_unpackstr_xmalloc(&object_ptr->archive_dir,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&object_ptr->archive_script,
				       &uint32_tmp, buffer);
		if (slurmdb_unpack_job_cond((void *)&object_ptr->job_cond,
					    protocol_version, buffer) ==
		    SLURM_ERROR)
			goto unpack_error;
		safe_unpack32(&object_ptr->purge_event, buffer);
		safe_unpack32(&object_ptr->purge_job, buffer);
		safe_unpack32(&object_ptr->purge_resv, buffer);
		safe_unpack32(&object_ptr->purge_step, buffer);
		safe_unpack32(&object_ptr->purge_suspend, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_archive_cond(object_ptr);
	*object = NULL;
	return SLURM_ERROR;

}

extern void slurmdb_pack_stats_msg(void *object, uint16_t protocol_version,
				   Buf buffer)
{
	slurmdb_stats_rec_t *stats_ptr = (slurmdb_stats_rec_t *) object;
	uint32_t i;

	if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
		/* Rollup statistics */
		i = 3;
		pack32(i, buffer);
		pack16_array(stats_ptr->rollup_count,    i, buffer);
		pack64_array(stats_ptr->rollup_time,     i, buffer);
		pack64_array(stats_ptr->rollup_max_time, i, buffer);

		/* RPC type statistics */
		for (i = 0; i < stats_ptr->type_cnt; i++) {
			if (stats_ptr->rpc_type_id[i] == 0)
				break;
		}
		pack32(i, buffer);
		pack16_array(stats_ptr->rpc_type_id,   i, buffer);
		pack32_array(stats_ptr->rpc_type_cnt,  i, buffer);
		pack64_array(stats_ptr->rpc_type_time, i, buffer);

		/* RPC user statistics */
		for (i = 1; i < stats_ptr->user_cnt; i++) {
			if (stats_ptr->rpc_user_id[i] == 0)
				break;
		}
		pack32(i, buffer);
		pack32_array(stats_ptr->rpc_user_id,   i, buffer);
		pack32_array(stats_ptr->rpc_user_cnt,  i, buffer);
		pack64_array(stats_ptr->rpc_user_time, i, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int slurmdb_unpack_stats_msg(void **object, uint16_t protocol_version,
				    Buf buffer)
{
	uint32_t uint32_tmp = 0;
	slurmdb_stats_rec_t *stats_ptr =
		xmalloc(sizeof(slurmdb_stats_rec_t));

	*object = stats_ptr;
	if (protocol_version >= SLURM_17_02_PROTOCOL_VERSION) {
		/* Rollup statistics */
		safe_unpack32(&uint32_tmp, buffer);
		if (uint32_tmp != 3)
			goto unpack_error;
		safe_unpack16_array(&stats_ptr->rollup_count, &uint32_tmp,
				    buffer);
		if (uint32_tmp != 3)
			goto unpack_error;
		safe_unpack64_array(&stats_ptr->rollup_time, &uint32_tmp,
				    buffer);
		if (uint32_tmp != 3)
			goto unpack_error;
		safe_unpack64_array(&stats_ptr->rollup_max_time, &uint32_tmp,
				    buffer);
		if (uint32_tmp != 3)
			goto unpack_error;

		/* RPC type statistics */
		safe_unpack32(&stats_ptr->type_cnt, buffer);
		safe_unpack16_array(&stats_ptr->rpc_type_id, &uint32_tmp,
				    buffer);
		if (uint32_tmp != stats_ptr->type_cnt)
			goto unpack_error;
		safe_unpack32_array(&stats_ptr->rpc_type_cnt, &uint32_tmp,
				    buffer);
		if (uint32_tmp != stats_ptr->type_cnt)
			goto unpack_error;
		safe_unpack64_array(&stats_ptr->rpc_type_time, &uint32_tmp,
				    buffer);
		if (uint32_tmp != stats_ptr->type_cnt)
			goto unpack_error;

		/* RPC user statistics */
		safe_unpack32(&stats_ptr->user_cnt, buffer);
		safe_unpack32_array(&stats_ptr->rpc_user_id, &uint32_tmp,
				    buffer);
		if (uint32_tmp != stats_ptr->user_cnt)
			goto unpack_error;
		safe_unpack32_array(&stats_ptr->rpc_user_cnt, &uint32_tmp,
				    buffer);
		if (uint32_tmp != stats_ptr->user_cnt)
			goto unpack_error;
		safe_unpack64_array(&stats_ptr->rpc_user_time, &uint32_tmp,
				    buffer);
		if (uint32_tmp != stats_ptr->user_cnt)
			goto unpack_error;
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurmdb_destroy_stats_rec(stats_ptr);
	*object = NULL;
	return SLURM_ERROR;
}

extern void slurmdb_pack_update_object(slurmdb_update_object_t *object,
				       uint16_t protocol_version, Buf buffer)
{
	uint32_t count = NO_VAL;
	ListIterator itr = NULL;
	void *slurmdb_object = NULL;
	void (*my_function) (void *object, uint16_t protocol_version,
			     Buf buffer);

	switch (object->type) {
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
		my_function = slurmdb_pack_assoc_rec;
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
	case SLURMDB_ADD_TRES:
		my_function = slurmdb_pack_tres_rec;
		break;
	case DBD_GOT_STATS:
		my_function = slurmdb_pack_stats_msg;
		break;
	case SLURMDB_UPDATE_FEDS:
		my_function = slurmdb_pack_federation_rec;
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
	if (count && (count != NO_VAL)) {
		itr = list_iterator_create(object->objects);
		while ((slurmdb_object = list_next(itr))) {
			(*(my_function))(
				slurmdb_object, protocol_version, buffer);
		}
		list_iterator_destroy(itr);
	}
}

extern int slurmdb_unpack_update_object(slurmdb_update_object_t **object,
					uint16_t protocol_version, Buf buffer)
{
	int i;
	uint32_t count;
	slurmdb_update_object_t *object_ptr =
		xmalloc(sizeof(slurmdb_update_object_t));
	void *slurmdb_object = NULL;
	int (*my_function) (void **object, uint16_t protocol_version,
			    Buf buffer);
	void (*my_destroy) (void *object);

	*object = object_ptr;

	safe_unpack16(&object_ptr->type, buffer);
	switch (object_ptr->type) {
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
		my_function = slurmdb_unpack_assoc_rec;
		my_destroy = slurmdb_destroy_assoc_rec;
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
	case SLURMDB_ADD_TRES:
		my_function = slurmdb_unpack_tres_rec;
		my_destroy = slurmdb_destroy_tres_rec;
		break;
	case DBD_GOT_STATS:
		my_function = slurmdb_unpack_stats_msg;
		my_destroy = slurmdb_destroy_stats_rec;
		break;
	case SLURMDB_UPDATE_FEDS:
		my_function = slurmdb_unpack_federation_rec;
		my_destroy  = slurmdb_destroy_federation_rec;
		break;
	case SLURMDB_UPDATE_NOTSET:
	default:
		error("unpack: unknown type set in update_object: %d",
		      object_ptr->type);
		goto unpack_error;
	}
	safe_unpack32(&count, buffer);
	if (count > NO_VAL)
		goto unpack_error;
	if (count != NO_VAL) {
		object_ptr->objects = list_create((*(my_destroy)));
		for (i = 0; i < count; i++) {
			if (((*(my_function))(&slurmdb_object,
					      protocol_version, buffer))
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

