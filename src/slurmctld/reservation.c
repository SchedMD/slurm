/*****************************************************************************\
 *  reservation.c - resource reservation management
 *****************************************************************************
 *  Copyright (C) 2009-2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#include "config.h"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/core_array.h"
#include "src/common/fd.h"
#include "src/common/hostlist.h"
#include "src/common/job_features.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_time.h"
#include "src/common/run_command.h"
#include "src/common/slurm_time.h"
#include "src/common/state_save.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/burst_buffer.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/node_features.h"
#include "src/interfaces/select.h"
#include "src/interfaces/topology.h"

#include "src/slurmctld/groups.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmscriptd.h"
#include "src/slurmctld/state_save.h"

#include "src/stepmgr/gres_stepmgr.h"
#include "src/stepmgr/stepmgr.h"

#define RESV_MAGIC	0x3b82

/* Permit sufficient time for slurmctld failover or other long delay before
 * considering a reservation time specification being invalid */
#define MAX_RESV_DELAY	600

#define MAX_RESV_COUNT	9999

/* No need to change we always pack SLURM_PROTOCOL_VERSION */
#define RESV_STATE_VERSION          "PROTOCOL_VERSION"

/*
 * Max number of ordered bitmaps a reservation can select against.
 * Last bitmap is always a NULL pointer
 */
#define MAX_BITMAPS 6
/* Available Nodes without any reservations */
#define SELECT_NOT_RSVD 0
/* Available Nodes including overlapping/main reserved nodes */
#define SELECT_OVR_RSVD 1
/* all available nodes in partition */
#define SELECT_AVL_RSVD 2
/* all online nodes in partition */
#define SELECT_ONL_RSVD 3
/* All possible nodes in partition */
#define SELECT_ALL_RSVD 4

static const char *select_node_bitmap_tags[] = {
	"SELECT_NOT_RSVD", "SELECT_OVR_RSVD", "SELECT_AVL_RSVD",
	"SELECT_ONL_RSVD", "SELECT_ALL_RSVD", NULL
};

time_t    last_resv_update = (time_t) 0;
list_t *resv_list = NULL;
static list_t *magnetic_resv_list = NULL;
uint32_t  top_suffix = 0;

typedef struct constraint_slot {
	time_t start;
	time_t end;
	uint32_t value;
	uint32_t duration;
	uint64_t flags;
} constraint_slot_t;

typedef struct {
	bitstr_t *core_bitmap;
	list_t *gres_list_exc;
	bitstr_t *node_bitmap;
} resv_select_t;

static int _advance_resv_time(slurmctld_resv_t *resv_ptr);
static void _advance_time(time_t *res_time, int day_cnt, int hour_cnt);
static int  _build_account_list(char *accounts, int *account_cnt,
				char ***account_list, bool *account_not);
static int  _build_uid_list(char *users, int *user_cnt, uid_t **user_list,
			    bool *user_not, bool strict);
static void _clear_job_resv(slurmctld_resv_t *resv_ptr);
static int _cmp_resv_id(void *x, void *y);
static slurmctld_resv_t *_copy_resv(slurmctld_resv_t *resv_orig_ptr);
static void _del_resv_rec(void *x);
static void _dump_resv_req(resv_desc_msg_t *resv_ptr, char *mode);
static int  _find_resv_id(void *x, void *key);
static int _find_resv_ptr(void *x, void *key);
static int  _find_resv_name(void *x, void *key);
static int  _generate_resv_id(void);
static void _generate_resv_name(resv_desc_msg_t *resv_ptr);
static int  _get_core_resrcs(slurmctld_resv_t *resv_ptr);
static uint32_t _get_job_duration(job_record_t *job_ptr, bool reboot);
static bool _is_account_valid(char *account);
static bool _is_resv_used(slurmctld_resv_t *resv_ptr);
static bool _job_overlap(time_t start_time, uint64_t flags,
			 bitstr_t *node_bitmap, char *resv_name);
static int _job_resv_check(void *x, void *arg);
static list_t *_list_dup(list_t *license_list);
static buf_t *_open_resv_state_file(char **state_file);
static void _pack_resv(slurmctld_resv_t *resv_ptr, buf_t *buffer,
		       bool internal, uint16_t protocol_version);
static void _pick_nodes(resv_desc_msg_t *resv_desc_ptr,
			resv_select_t *resv_select,
			resv_select_t *resv_select_ret);
static int _pick_nodes_ordered(resv_desc_msg_t *resv_desc_ptr,
			       resv_select_t *resv_select,
			       int resv_select_cnt,
			       resv_select_t *resv_select_ret,
			       const char **bitmap_tags);
static void _pick_nodes_by_feature_node_cnt(bitstr_t *avail_bitmap,
					    resv_desc_msg_t *resv_desc_ptr,
					    resv_select_t *resv_select_ret,
					    int total_node_cnt,
					    list_t *feature_list);
static bitstr_t *_pick_node_cnt(resv_desc_msg_t *resv_desc_ptr,
				resv_select_t *resv_select,
				uint32_t node_cnt);
static int  _post_resv_create(slurmctld_resv_t *resv_ptr);
static int  _post_resv_delete(slurmctld_resv_t *resv_ptr);
static int  _post_resv_update(slurmctld_resv_t *resv_ptr,
			      slurmctld_resv_t *old_resv_ptr);
static int  _resize_resv(slurmctld_resv_t *resv_ptr, uint32_t node_cnt);
static void _restore_resv(slurmctld_resv_t *dest_resv,
			  slurmctld_resv_t *src_resv);
static bool _resv_overlap(resv_desc_msg_t *resv_desc_ptr,
			  bitstr_t *node_bitmap,
			  slurmctld_resv_t *this_resv_ptr);
static bool _resv_time_overlap(resv_desc_msg_t *resv_desc_ptr,
			       slurmctld_resv_t *resv_ptr);
static void _run_script(char *script, slurmctld_resv_t *resv_ptr, char *name);
static int  _select_nodes(resv_desc_msg_t *resv_desc_ptr,
			  part_record_t **part_ptr,
			  resv_select_t *resv_select_ret);
static int  _set_assoc_list(slurmctld_resv_t *resv_ptr);
static void _set_tres_cnt(slurmctld_resv_t *resv_ptr,
			  slurmctld_resv_t *old_resv_ptr);
static void _set_nodes_flags(slurmctld_resv_t *resv_ptr, time_t now,
			     uint32_t flags, bool reset_all);
static int  _update_account_list(slurmctld_resv_t *resv_ptr,
				 char *accounts);
static int  _update_uid_list(slurmctld_resv_t *resv_ptr, char *users);
static int _update_group_uid_list(slurmctld_resv_t *resv_ptr, char *groups);
static int _update_job_resv_list_str(void *x, void *arg);
static int _update_resv_pend_cnt(void *x, void *arg);
static void _validate_all_reservations(void);
static int  _valid_job_access_resv(job_record_t *job_ptr,
				   slurmctld_resv_t *resv_ptr,
				   bool show_security_violation_error);
static bool _validate_one_reservation(slurmctld_resv_t *resv_ptr);
static void _validate_node_choice(slurmctld_resv_t *resv_ptr);
static bool _validate_user_access(slurmctld_resv_t *resv_ptr,
				  list_t *user_assoc_list, uid_t uid);

static void _free_resv_select_members(resv_select_t *resv_select)
{
	if (!resv_select)
		return;

	FREE_NULL_BITMAP(resv_select->core_bitmap);
	FREE_NULL_LIST(resv_select->gres_list_exc);
	FREE_NULL_BITMAP(resv_select->node_bitmap);
}

static int _switch_select_alloc_gres(void *x, void *arg)
{
	gres_state_t *gres_state_job = x;
	gres_job_state_t *gres_js = gres_state_job->gres_data;

	/*
	 * Until a job is allocated ->node_cnt isn't set ->total_node_cnt is
	 * used.
	 */
	gres_js->node_cnt = gres_js->total_node_cnt;
	gres_js->total_node_cnt = 0;

	if (gres_js->gres_bit_alloc) {
		/* This should never happen */
		for (int i = 0; i < gres_js->node_cnt; i++) {
			FREE_NULL_BITMAP(gres_js->gres_bit_alloc[i]);
		}
		xfree(gres_js->gres_bit_alloc);
	}
	gres_js->gres_bit_alloc = gres_js->gres_bit_select;
	gres_js->gres_bit_select = NULL;
	xfree(gres_js->gres_cnt_node_alloc);
	gres_js->gres_cnt_node_alloc = gres_js->gres_cnt_node_select;
	gres_js->gres_cnt_node_select = NULL;
	return 0;
}

static int _parse_tres_str(resv_desc_msg_t *resv_desc_ptr)
{
	char *tmp_str, *tres_sub_str;

	if (!resv_desc_ptr->tres_str)
		return SLURM_SUCCESS;

	/*
	 * Here we need to verify all the TRES (including GRES) are real TRES.
	 */
	if (!assoc_mgr_valid_tres_cnt(resv_desc_ptr->tres_str, true))
		return ESLURM_INVALID_TRES;

	/*
	 * There are a few different ways to request a tres string, this
	 * will format it correctly for the rest of Slurm.
	 */
	tmp_str = slurm_get_tres_sub_string(
		resv_desc_ptr->tres_str, NULL, NO_VAL, true, true);

	if (!tmp_str)
		return ESLURM_INVALID_TRES;
	xfree(resv_desc_ptr->tres_str);
	resv_desc_ptr->tres_str = tmp_str;
	tmp_str = NULL;

	tres_sub_str = slurm_get_tres_sub_string(
		resv_desc_ptr->tres_str, "license", NO_VAL,
		false, false);
	if (tres_sub_str) {
		if (resv_desc_ptr->licenses)
			return ESLURM_INVALID_LICENSES;
		resv_desc_ptr->licenses = tres_sub_str;
		tres_sub_str = NULL;
	}

	tres_sub_str = slurm_get_tres_sub_string(
		resv_desc_ptr->tres_str, "node", NO_VAL,
		false, false);
	if (tres_sub_str) {
		if (resv_desc_ptr->node_cnt != NO_VAL)
			return ESLURM_INVALID_NODE_COUNT;
		resv_desc_ptr->node_cnt = atoi(tres_sub_str + 1);
		xfree(tres_sub_str);
	}

	tres_sub_str = slurm_get_tres_sub_string(
		resv_desc_ptr->tres_str, "cpu", NO_VAL,
		false, false);
	if (tres_sub_str) {
		if (resv_desc_ptr->core_cnt != NO_VAL)
			return ESLURM_INVALID_CPU_COUNT;
		resv_desc_ptr->core_cnt = atoi(tres_sub_str + 1);
		if ((resv_desc_ptr->flags & RESERVE_TRES_PER_NODE) &&
		    (resv_desc_ptr->node_cnt != NO_VAL))
			resv_desc_ptr->core_cnt *= resv_desc_ptr->node_cnt;
		xfree(tres_sub_str);
	}

	tres_sub_str = slurm_get_tres_sub_string(
		resv_desc_ptr->tres_str, "bb", NO_VAL,
		false, false);
	if (tres_sub_str) {
		if (resv_desc_ptr->burst_buffer)
			return ESLURM_INVALID_BURST_BUFFER_REQUEST;
		resv_desc_ptr->burst_buffer = tres_sub_str;
		tres_sub_str = NULL;
	}

	return SLURM_SUCCESS;
}

static bitstr_t *_resv_select(resv_desc_msg_t *resv_desc_ptr,
			      resv_select_t *resv_select)
{
	job_record_t *job_ptr;
	resv_exc_t resv_exc = { 0 };
	int rc;

	xassert(avail_node_bitmap);
	xassert(resv_desc_ptr);
	xassert(resv_desc_ptr->job_ptr);

	resv_exc.core_bitmap = resv_select->core_bitmap;
	resv_exc.exc_cores = core_bitmap_to_array(resv_exc.core_bitmap);
	resv_exc.gres_list_exc = resv_select->gres_list_exc;

	job_ptr = resv_desc_ptr->job_ptr;

	rc = select_g_job_test(
		job_ptr, resv_select->node_bitmap, job_ptr->details->min_nodes,
		job_ptr->details->max_nodes,
		job_ptr->details->min_nodes,
		SELECT_MODE_WILL_RUN, NULL, NULL,
		&resv_exc,
		NULL);

	free_core_array(&resv_exc.exc_cores);

	if (rc != SLURM_SUCCESS) {
		return NULL;
	}

	if ((resv_desc_ptr->flags & RESERVE_FLAG_GRES_REQ) ||
	    (resv_desc_ptr->core_cnt != NO_VAL)) {
		if (resv_select->core_bitmap)
			bit_clear_all(resv_select->core_bitmap);

		/*
		 * ncpus will usually only have a core count here, total_cpus is
		 * always correct.
		 */
		job_ptr->job_resrcs->ncpus = job_ptr->total_cpus;
		add_job_to_cores(job_ptr->job_resrcs,
				 &resv_select->core_bitmap);
		if (job_ptr->gres_list_req) {
			(void) list_for_each(job_ptr->gres_list_req,
					     _switch_select_alloc_gres,
					     NULL);
		}
	} else
		free_job_resources(&job_ptr->job_resrcs);

	return bit_copy(resv_select->node_bitmap);
}

static void _set_boot_time(slurmctld_resv_t *resv_ptr)
{
	resv_ptr->boot_time = 0;
	if (!resv_ptr->node_bitmap)
		return;

	if (node_features_g_overlap(resv_ptr->node_bitmap))
		resv_ptr->boot_time = node_features_g_boot_time();
}

/* Advance res_time by the specified day and hour counts,
 * account for daylight savings time */
static void _advance_time(time_t *res_time, int day_cnt, int hour_cnt)
{
	time_t save_time = *res_time;
	struct tm time_tm;

	localtime_r(res_time, &time_tm);
	time_tm.tm_mday += day_cnt;
	time_tm.tm_hour += hour_cnt;
	*res_time = slurm_mktime(&time_tm);
	if (*res_time == (time_t)(-1)) {
		error("Could not compute reservation time %lu",
		      (long unsigned int) save_time);
		*res_time = save_time + (24 * 60 * 60);
	}
}

static list_t *_list_dup(list_t *license_list)
{
	list_itr_t *iter;
	licenses_t *license_src, *license_dest;
	list_t *lic_list = NULL;

	if (!license_list)
		return lic_list;

	lic_list = list_create(license_free_rec);
	iter = list_iterator_create(license_list);
	while ((license_src = list_next(iter))) {
		license_dest = xmalloc(sizeof(licenses_t));
		license_dest->name = xstrdup(license_src->name);
		license_dest->used = license_src->used;
		list_push(lic_list, license_dest);
	}
	list_iterator_destroy(iter);
	return lic_list;
}

static slurmctld_resv_t *_copy_resv(slurmctld_resv_t *resv_orig_ptr)
{
	slurmctld_resv_t *resv_copy_ptr;
	int i;

	xassert(resv_orig_ptr->magic == RESV_MAGIC);
	resv_copy_ptr = xmalloc(sizeof(slurmctld_resv_t));
	resv_copy_ptr->accounts = xstrdup(resv_orig_ptr->accounts);
	resv_copy_ptr->boot_time = resv_orig_ptr->boot_time;
	resv_copy_ptr->burst_buffer = xstrdup(resv_orig_ptr->burst_buffer);
	resv_copy_ptr->account_cnt = resv_orig_ptr->account_cnt;
	resv_copy_ptr->account_list = xcalloc(resv_orig_ptr->account_cnt,
					      sizeof(char *));
	for (i = 0; i < resv_copy_ptr->account_cnt; i++) {
		resv_copy_ptr->account_list[i] =
				xstrdup(resv_orig_ptr->account_list[i]);
	}
	resv_copy_ptr->assoc_list = xstrdup(resv_orig_ptr->assoc_list);
	if (resv_orig_ptr->core_bitmap) {
		resv_copy_ptr->core_bitmap = bit_copy(resv_orig_ptr->
						      core_bitmap);
	}

	resv_copy_ptr->ctld_flags = resv_orig_ptr->ctld_flags;

	resv_copy_ptr->core_cnt = resv_orig_ptr->core_cnt;
	if (resv_orig_ptr->core_resrcs) {
		resv_copy_ptr->core_resrcs = copy_job_resources(resv_orig_ptr->
								core_resrcs);
	}
	resv_copy_ptr->duration = resv_orig_ptr->duration;
	resv_copy_ptr->end_time = resv_orig_ptr->end_time;
	resv_copy_ptr->features = xstrdup(resv_orig_ptr->features);
	resv_copy_ptr->flags = resv_orig_ptr->flags;
	resv_copy_ptr->groups = xstrdup(resv_orig_ptr->groups);
	resv_copy_ptr->job_pend_cnt = resv_orig_ptr->job_pend_cnt;
	resv_copy_ptr->job_run_cnt = resv_orig_ptr->job_run_cnt;
	resv_copy_ptr->licenses = xstrdup(resv_orig_ptr->licenses);
	resv_copy_ptr->license_list = _list_dup(resv_orig_ptr->
						license_list);
	resv_copy_ptr->magic = resv_orig_ptr->magic;
	resv_copy_ptr->name = xstrdup(resv_orig_ptr->name);
	if (resv_orig_ptr->node_bitmap) {
		resv_copy_ptr->node_bitmap =
			bit_copy(resv_orig_ptr->node_bitmap);
	}
	resv_copy_ptr->node_cnt = resv_orig_ptr->node_cnt;
	resv_copy_ptr->node_list = xstrdup(resv_orig_ptr->node_list);
	resv_copy_ptr->partition = xstrdup(resv_orig_ptr->partition);
	resv_copy_ptr->part_ptr = resv_orig_ptr->part_ptr;
	resv_copy_ptr->resv_id = resv_orig_ptr->resv_id;
	resv_copy_ptr->start_time = resv_orig_ptr->start_time;
	resv_copy_ptr->start_time_first = resv_orig_ptr->start_time_first;
	resv_copy_ptr->start_time_prev = resv_orig_ptr->start_time_prev;
	resv_copy_ptr->tres_str = xstrdup(resv_orig_ptr->tres_str);
	resv_copy_ptr->tres_fmt_str = xstrdup(resv_orig_ptr->tres_fmt_str);
	resv_copy_ptr->users = xstrdup(resv_orig_ptr->users);
	resv_copy_ptr->user_cnt = resv_orig_ptr->user_cnt;
	resv_copy_ptr->user_list = xcalloc(resv_orig_ptr->user_cnt,
					   sizeof(uid_t));
	for (i = 0; i < resv_copy_ptr->user_cnt; i++)
		resv_copy_ptr->user_list[i] = resv_orig_ptr->user_list[i];

	return resv_copy_ptr;
}

/* Move the contents of src_resv into dest_resv.
 * NOTE: This is a destructive function with respect to the contents of
 *       src_resv. The data structure src_resv is suitable only for destruction
 *       after this function is called */
static void _restore_resv(slurmctld_resv_t *dest_resv,
			  slurmctld_resv_t *src_resv)
{
	int i;

	xfree(dest_resv->accounts);
	dest_resv->accounts = src_resv->accounts;
	src_resv->accounts = NULL;

	for (i = 0; i < dest_resv->account_cnt; i++)
		xfree(dest_resv->account_list[i]);
	xfree(dest_resv->account_list);
	dest_resv->account_cnt = src_resv->account_cnt;
	src_resv->account_cnt = 0;
	dest_resv->account_list = src_resv->account_list;
	src_resv->account_list = NULL;

	xfree(dest_resv->assoc_list);
	dest_resv->assoc_list = src_resv->assoc_list;
	src_resv->assoc_list = NULL;

	dest_resv->boot_time = src_resv->boot_time;

	xfree(dest_resv->burst_buffer);
	dest_resv->burst_buffer = src_resv->burst_buffer;
	src_resv->burst_buffer = NULL;

	FREE_NULL_BITMAP(dest_resv->core_bitmap);
	dest_resv->core_bitmap = src_resv->core_bitmap;
	src_resv->core_bitmap = NULL;

	dest_resv->core_cnt = src_resv->core_cnt;

	free_job_resources(&dest_resv->core_resrcs);
	dest_resv->core_resrcs = src_resv->core_resrcs;
	src_resv->core_resrcs = NULL;

	dest_resv->ctld_flags = src_resv->ctld_flags;

	dest_resv->duration = src_resv->duration;
	dest_resv->end_time = src_resv->end_time;

	xfree(dest_resv->features);
	dest_resv->features = src_resv->features;
	src_resv->features = NULL;

	dest_resv->flags = src_resv->flags;
	dest_resv->job_pend_cnt = src_resv->job_pend_cnt;
	dest_resv->job_run_cnt = src_resv->job_run_cnt;

	xfree(dest_resv->groups);
	dest_resv->groups = src_resv->groups;
	src_resv->groups = NULL;

	xfree(dest_resv->licenses);
	dest_resv->licenses = src_resv->licenses;
	src_resv->licenses = NULL;

	FREE_NULL_LIST(dest_resv->license_list);
	dest_resv->license_list = src_resv->license_list;
	src_resv->license_list = NULL;

	dest_resv->magic = src_resv->magic;

	xfree(dest_resv->name);
	dest_resv->name = src_resv->name;
	src_resv->name = NULL;

	FREE_NULL_BITMAP(dest_resv->node_bitmap);
	dest_resv->node_bitmap = src_resv->node_bitmap;
	src_resv->node_bitmap = NULL;

	dest_resv->node_cnt = src_resv->node_cnt;

	xfree(dest_resv->node_list);
	dest_resv->node_list = src_resv->node_list;
	src_resv->node_list = NULL;

	xfree(dest_resv->partition);
	dest_resv->partition = src_resv->partition;
	src_resv->partition = NULL;

	dest_resv->part_ptr = src_resv->part_ptr;
	dest_resv->resv_id = src_resv->resv_id;
	dest_resv->start_time = src_resv->start_time;
	dest_resv->start_time_first = src_resv->start_time_first;
	dest_resv->start_time_prev = src_resv->start_time_prev;

	xfree(dest_resv->tres_str);
	dest_resv->tres_str = src_resv->tres_str;
	src_resv->tres_str = NULL;

	xfree(dest_resv->tres_fmt_str);
	dest_resv->tres_fmt_str = src_resv->tres_fmt_str;
	src_resv->tres_fmt_str = NULL;

	xfree(dest_resv->users);
	dest_resv->users = src_resv->users;
	src_resv->users = NULL;

	dest_resv->user_cnt = src_resv->user_cnt;
	xfree(dest_resv->user_list);
	dest_resv->user_list = src_resv->user_list;
	src_resv->user_list = NULL;
}

static void _del_resv_rec(void *x)
{
	int i;
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) x;

	if (resv_ptr) {
		/*
		 * If shutting down magnetic_resv_list is already freed, meaning
		 * we don't need to remove anything from it.
		 */
		if (magnetic_resv_list &&
		    (resv_ptr->flags & RESERVE_FLAG_MAGNETIC)) {
			int cnt;
			cnt = list_delete_all(magnetic_resv_list,
					      _find_resv_ptr,
					      resv_ptr);
			if (cnt > 1) {
				error("%s: magnetic_resv_list contained %d references to %s",
				      __func__, cnt, resv_ptr->name);
			}
		}

		xassert(resv_ptr->magic == RESV_MAGIC);
		resv_ptr->magic = 0;
		xfree(resv_ptr->accounts);
		for (i = 0; i < resv_ptr->account_cnt; i++)
			xfree(resv_ptr->account_list[i]);
		xfree(resv_ptr->account_list);
		xfree(resv_ptr->assoc_list);
		xfree(resv_ptr->burst_buffer);
		FREE_NULL_BITMAP(resv_ptr->core_bitmap);
		free_job_resources(&resv_ptr->core_resrcs);
		xfree(resv_ptr->features);
		FREE_NULL_LIST(resv_ptr->gres_list_alloc);
		xfree(resv_ptr->groups);
		FREE_NULL_LIST(resv_ptr->license_list);
		xfree(resv_ptr->licenses);
		xfree(resv_ptr->name);
		FREE_NULL_BITMAP(resv_ptr->node_bitmap);
		xfree(resv_ptr->node_list);
		xfree(resv_ptr->partition);
		xfree(resv_ptr->tres_str);
		xfree(resv_ptr->tres_fmt_str);
		xfree(resv_ptr->users);
		xfree(resv_ptr->user_list);
		xfree(resv_ptr);
	}
}

static void _create_resv_lists(bool flush)
{
	if (flush && resv_list) {
		list_flush(magnetic_resv_list);
		list_flush(resv_list);
		return;
	}

	if (!resv_list)
		resv_list = list_create(_del_resv_rec);
	if (!magnetic_resv_list)
		magnetic_resv_list = list_create(NULL);
}

static void _add_resv_to_lists(slurmctld_resv_t *resv_ptr)
{
	xassert(resv_list);
	xassert(magnetic_resv_list);

	list_append(resv_list, resv_ptr);
	if (resv_ptr->flags & RESERVE_FLAG_MAGNETIC)
		list_append(magnetic_resv_list, resv_ptr);
}

static int _queue_magnetic_resv(void *x, void *key)
{
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) x;
	job_queue_req_t *job_queue_req = (job_queue_req_t *) key;

	xassert(resv_ptr->magic == RESV_MAGIC);

	if (!(resv_ptr->flags & RESERVE_FLAG_MAGNETIC) ||
	    (_valid_job_access_resv(job_queue_req->job_ptr, resv_ptr, false) !=
	     SLURM_SUCCESS))
		return 0;

	job_queue_req->resv_ptr = resv_ptr;
	job_queue_append_internal(job_queue_req);

	return 0;
}

static int _cmp_resv_id(void *x, void *y)
{
	slurmctld_resv_t *resv_ptr1 = *(slurmctld_resv_t **) x;
	slurmctld_resv_t *resv_ptr2 = *(slurmctld_resv_t **) y;

	if (resv_ptr1->resv_id < resv_ptr2->resv_id)
		return -1;
	if (resv_ptr1->resv_id > resv_ptr2->resv_id)
		return 1;
	return 0;
}

static int _find_job_with_resv_ptr(void *x, void *key)
{
	job_record_t *job_ptr = (job_record_t *) x;
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) key;

	if (job_ptr->resv_ptr == resv_ptr)
		return 1;
	if (job_ptr->resv_list &&
	    list_find_first(job_ptr->resv_list, _find_resv_ptr, resv_ptr))
		return 1;
	return 0;
}

static int _find_running_job_with_resv_ptr(void *x, void *key)
{
	job_record_t *job_ptr = (job_record_t *) x;
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) key;

	if ((!IS_JOB_FINISHED(job_ptr)) &&
	    _find_job_with_resv_ptr(job_ptr, resv_ptr))
		return 1;
	return 0;
}

static int _find_resv_id(void *x, void *key)
{
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) x;
	uint32_t *resv_id = (uint32_t *) key;

	xassert(resv_ptr->magic == RESV_MAGIC);

	if (resv_ptr->resv_id != *resv_id)
		return 0;
	else
		return 1;	/* match */
}

static int _find_resv_ptr(void *x, void *key)
{
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) x;
	slurmctld_resv_t *resv_ptr_key = (slurmctld_resv_t *) key;

	xassert(resv_ptr->magic == RESV_MAGIC);

	if (resv_ptr != resv_ptr_key)
		return 0;
	else
		return 1;	/* match */
}

static int _find_resv_name(void *x, void *key)
{
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) x;

	xassert(resv_ptr->magic == RESV_MAGIC);

	if (xstrcmp(resv_ptr->name, (char *) key))
		return 0;
	else
		return 1;	/* match */
}

static int _foreach_clear_job_resv(void *x, void *key)
{
	job_record_t *job_ptr = (job_record_t *) x;
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) key;

	/*
	 * Do this before checking if we have the correct reservation or
	 * not. Since this could be set whether or not the job requested a
	 * reservation.
	 */
	if ((resv_ptr->flags & RESERVE_FLAG_MAINT) &&
	    (job_ptr->state_reason == WAIT_NODE_NOT_AVAIL) &&
	    !xstrcmp(job_ptr->state_desc,
		     "ReqNodeNotAvail, Reserved for maintenance")) {
		/*
		 * In case of cluster maintenance many jobs may get this
		 * state set. If we wait for scheduler to update
		 * the reason it may take long time after the
		 * reservation completion. Instead of that clear it
		 * when MAINT reservation ends.
		 */
		job_ptr->state_reason = WAIT_NO_REASON;
		xfree(job_ptr->state_desc);
	}

	if (!_find_job_with_resv_ptr(job_ptr, resv_ptr))
		return 0;

	if (!IS_JOB_FINISHED(job_ptr)) {
		info("%pJ linked to defunct reservation %s, clearing that reservation",
		     job_ptr, resv_ptr->name);
	}

	job_ptr->resv_id = 0;
	job_ptr->resv_ptr = NULL;
	xfree(job_ptr->resv_name);

	if (job_ptr->resv_list) {
		int resv_cnt;
		list_remove_first(job_ptr->resv_list, _find_resv_ptr, resv_ptr);
		job_ptr->resv_ptr = list_peek(job_ptr->resv_list);
		resv_cnt = list_count(job_ptr->resv_list);
		if (resv_cnt <= 0) {
			FREE_NULL_LIST(job_ptr->resv_list);
		} else if (resv_cnt == 1) {
			job_ptr->resv_id = job_ptr->resv_ptr->resv_id;
			job_ptr->resv_name = xstrdup(job_ptr->resv_ptr->name);
			FREE_NULL_LIST(job_ptr->resv_list);
		} else {
			list_for_each(job_ptr->resv_list,
				      _update_job_resv_list_str,
				      &job_ptr->resv_name);
		}
	}

	if (!(resv_ptr->flags & RESERVE_FLAG_NO_HOLD_JOBS) &&
	    IS_JOB_PENDING(job_ptr) && !job_ptr->resv_ptr &&
	    (job_ptr->state_reason != WAIT_HELD)) {
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = WAIT_RESV_DELETED;
		job_state_set_flag(job_ptr, JOB_RESV_DEL_HOLD);
		xstrfmtcat(job_ptr->state_desc,
			   "Reservation %s was deleted",
			   resv_ptr->name);
		debug("%s: Holding %pJ, reservation %s was deleted",
		      __func__, job_ptr, resv_ptr->name);
		job_ptr->priority = 0;	/* Hold job */
	}

	return 0;
}

static int _update_job_resv_list_str(void *x, void *arg)
{
	slurmctld_resv_t *resv_ptr = x;
	char **resv_name = arg;
	xstrfmtcat(*resv_name, "%s%s", *resv_name ? "," : "", resv_ptr->name);

	return 0;
}

static int _update_resv_pend_cnt(void *x, void *arg)
{
	slurmctld_resv_t *resv_ptr = x;
	xassert(resv_ptr->magic == RESV_MAGIC);
	resv_ptr->job_pend_cnt++;

	return 0;
}

static void _dump_resv_req(resv_desc_msg_t *resv_ptr, char *mode)
{

	char start_str[256] = "-1", end_str[256] = "-1", *flag_str = NULL;
	int duration;

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION))
		return;

	if (resv_ptr->start_time != (time_t) NO_VAL) {
		slurm_make_time_str(&resv_ptr->start_time,
				    start_str, sizeof(start_str));
	}
	if (resv_ptr->end_time != (time_t) NO_VAL) {
		slurm_make_time_str(&resv_ptr->end_time,
				    end_str,  sizeof(end_str));
	}
	if (resv_ptr->flags != NO_VAL64) {
		reserve_info_t resv_info = {
			.flags = resv_ptr->flags,
			.purge_comp_time = resv_ptr->purge_comp_time
		};
		flag_str = reservation_flags_string(&resv_info);
	}
	if (resv_ptr->duration == NO_VAL)
		duration = -1;
	else
		duration = resv_ptr->duration;

	info("%s: Name=%s StartTime=%s EndTime=%s Duration=%d Flags=%s NodeCnt=%u CoreCnt=%u NodeList=%s Features=%s PartitionName=%s Users=%s Groups=%s Accounts=%s Licenses=%s BurstBuffer=%s TRES=%s Comment=%s",
	     mode, resv_ptr->name, start_str, end_str, duration,
	     flag_str, resv_ptr->node_cnt, resv_ptr->core_cnt,
	     resv_ptr->node_list,
	     resv_ptr->features, resv_ptr->partition,
	     resv_ptr->users, resv_ptr->groups, resv_ptr->accounts,
	     resv_ptr->licenses,
	     resv_ptr->burst_buffer, resv_ptr->tres_str,
	     resv_ptr->comment);

	xfree(flag_str);
}

static int _generate_resv_id(void)
{
	int i;

	for (i = 0; i < MAX_RESV_COUNT; i++) {
		if (top_suffix >= MAX_RESV_COUNT)
			top_suffix = 1;	/* wrap around */
		else
			top_suffix++;
		if (!list_find_first(resv_list, _find_resv_id, &top_suffix))
			return SLURM_SUCCESS;
	}

	error("%s: Too many reservations in the system, can't create any more.",
	      __func__);

	return ESLURM_RESERVATION_INVALID;
}

static void _generate_resv_name(resv_desc_msg_t *resv_ptr)
{
	char *key, *name, *sep;
	int len;

	/* Generate name prefix, based upon the first account
	 * name if provided otherwise first user name */
	if (resv_ptr->accounts && resv_ptr->accounts[0])
		key = resv_ptr->accounts;
	else if (resv_ptr->users && resv_ptr->users[0])
		key = resv_ptr->users;
	else if (resv_ptr->groups && resv_ptr->groups[0])
		key = resv_ptr->groups;
	else
		key = "resv";
	if (key[0] == '-')
		key++;
	sep = strchr(key, ',');
	if (sep)
		len = sep - key;
	else
		len = strlen(key);

	name = xstrdup_printf("%.*s_%d", len, key, top_suffix);

	xfree(resv_ptr->name);
	resv_ptr->name = name;
}

/* Validate an account name */
static bool _is_account_valid(char *account)
{
	slurmdb_assoc_rec_t assoc_rec, *assoc_ptr;

	if (!(accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS))
		return true;	/* don't worry about account validity */

	memset(&assoc_rec, 0, sizeof(slurmdb_assoc_rec_t));
	assoc_rec.uid       = NO_VAL;
	assoc_rec.acct      = account;

	if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
				    accounting_enforce, &assoc_ptr, false)) {
		return false;
	}
	return true;
}

/* Since the returned assoc_list is full of pointers from the
 * assoc_mgr_association_list assoc_mgr_lock_t READ_LOCK on
 * associations must be set before calling this function and while
 * handling it after a return.
 */
static int _append_acct_to_assoc_list(list_t *assoc_list,
				      slurmdb_assoc_rec_t *assoc)
{
	int rc = ESLURM_INVALID_ACCOUNT;
	slurmdb_assoc_rec_t *assoc_ptr = NULL;

	xassert(assoc->uid == NO_VAL);

	if (assoc_mgr_fill_in_assoc(
		    acct_db_conn, assoc,
		    accounting_enforce,
		    &assoc_ptr, true)) {
		if (accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS) {
			error("No association for user %u and account %s",
			      assoc->uid, assoc->acct);
		} else {
			verbose("No association for user %u and account %s",
				assoc->uid, assoc->acct);
			rc = SLURM_SUCCESS;
		}

	}
	if (assoc_ptr) {
		list_append(assoc_list, assoc_ptr);
		rc = SLURM_SUCCESS;
	}

	return rc;
}

/* Set a association list based upon accounts and users */
static int _set_assoc_list(slurmctld_resv_t *resv_ptr)
{
	int rc = SLURM_SUCCESS, i = 0, j = 0;
	list_t *assoc_list_allow = NULL, *assoc_list_deny = NULL;
	list_t *assoc_list = NULL;
	slurmdb_assoc_rec_t assoc, *assoc_ptr = NULL;
	assoc_mgr_lock_t locks = { .assoc = READ_LOCK, .user = READ_LOCK };


	/* no need to do this if we can't ;) */
	if (!slurm_with_slurmdbd())
		return rc;

	assoc_list_allow = list_create(NULL);
	assoc_list_deny  = list_create(NULL);

	memset(&assoc, 0, sizeof(slurmdb_assoc_rec_t));
	xfree(resv_ptr->assoc_list);

	assoc_mgr_lock(&locks);
	if (resv_ptr->account_cnt && resv_ptr->user_cnt) {
		if (!(resv_ptr->ctld_flags &
		      (RESV_CTLD_USER_NOT | RESV_CTLD_ACCT_NOT))) {
			/* Add every association that matches both account
			 * and user */
			for (i=0; i < resv_ptr->user_cnt; i++) {
				for (j=0; j < resv_ptr->account_cnt; j++) {
					memset(&assoc, 0,
					       sizeof(slurmdb_assoc_rec_t));
					assoc.acct = resv_ptr->account_list[j];
					assoc.uid  = resv_ptr->user_list[i];
					rc = assoc_mgr_get_user_assocs(
						acct_db_conn, &assoc,
						accounting_enforce,
						assoc_list_allow);
					if (rc != SLURM_SUCCESS)
						goto end_it;
				}
			}
		} else {
			if (resv_ptr->ctld_flags & RESV_CTLD_USER_NOT)
				assoc_list = assoc_list_deny;
			else
				assoc_list = assoc_list_allow;
			for (i=0; i < resv_ptr->user_cnt; i++) {
				memset(&assoc, 0,
				       sizeof(slurmdb_assoc_rec_t));
				assoc.uid = resv_ptr->user_list[i];
				rc = assoc_mgr_get_user_assocs(
					    acct_db_conn, &assoc,
					    accounting_enforce,
					    assoc_list);
				if (rc != SLURM_SUCCESS) {
					/*
					 * When using groups we might have users
					 * that don't have associations, just
					 * skip them
					 */
					if (resv_ptr->groups) {
						rc = SLURM_SUCCESS;
						continue;
					}
					error("No associations for UID %u",
					      assoc.uid);
					rc = ESLURM_INVALID_ACCOUNT;
					goto end_it;
				}
			}
			if (resv_ptr->ctld_flags & RESV_CTLD_ACCT_NOT)
				assoc_list = assoc_list_deny;
			else
				assoc_list = assoc_list_allow;
			for (j=0; j < resv_ptr->account_cnt; j++) {
				memset(&assoc, 0,
				       sizeof(slurmdb_assoc_rec_t));
				assoc.acct = resv_ptr->account_list[j];
				assoc.uid  = NO_VAL;
				rc = _append_acct_to_assoc_list(assoc_list,
								&assoc);
				if (rc != SLURM_SUCCESS)
					goto end_it;
			}
		}
	} else if (resv_ptr->user_cnt) {
		if (resv_ptr->ctld_flags & RESV_CTLD_USER_NOT)
			assoc_list = assoc_list_deny;
		else
			assoc_list = assoc_list_allow;
		for (i=0; i < resv_ptr->user_cnt; i++) {
			memset(&assoc, 0, sizeof(slurmdb_assoc_rec_t));
			assoc.uid = resv_ptr->user_list[i];
			rc = assoc_mgr_get_user_assocs(
				    acct_db_conn, &assoc,
				    accounting_enforce, assoc_list);
			if (rc != SLURM_SUCCESS) {
				/*
				 * When using groups we might have users that
				 * don't have associations, just skip them
				 */
				if (resv_ptr->groups) {
					rc = SLURM_SUCCESS;
					continue;
				}
				error("No associations for UID %u",
				      assoc.uid);
				rc = ESLURM_INVALID_ACCOUNT;
				goto end_it;
			}
		}
	} else if (resv_ptr->account_cnt) {
		if (resv_ptr->ctld_flags & RESV_CTLD_ACCT_NOT)
			assoc_list = assoc_list_deny;
		else
			assoc_list = assoc_list_allow;
		for (i=0; i < resv_ptr->account_cnt; i++) {
			memset(&assoc, 0, sizeof(slurmdb_assoc_rec_t));
			assoc.acct = resv_ptr->account_list[i];
			assoc.uid  = NO_VAL;
			if ((rc = _append_acct_to_assoc_list(assoc_list,
							     &assoc)) !=
							     SLURM_SUCCESS) {
				goto end_it;
			}
		}
	} else if (accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS) {
		error("We need at least 1 user or 1 account to "
		      "create a reservtion.");
		rc = SLURM_ERROR;
	}

	xfree(resv_ptr->assoc_list);	/* clear for modify */
	if (list_count(assoc_list_allow)) {
		list_itr_t *itr = list_iterator_create(assoc_list_allow);
		while ((assoc_ptr = list_next(itr))) {
			if (resv_ptr->assoc_list) {
				xstrfmtcat(resv_ptr->assoc_list, "%u,",
					   assoc_ptr->id);
			} else {
				xstrfmtcat(resv_ptr->assoc_list, ",%u,",
					   assoc_ptr->id);
			}
		}
		list_iterator_destroy(itr);
	}
	if (list_count(assoc_list_deny)) {
		list_itr_t *itr = list_iterator_create(assoc_list_deny);
		while ((assoc_ptr = list_next(itr))) {
			if (resv_ptr->assoc_list) {
				xstrfmtcat(resv_ptr->assoc_list, "-%u,",
					   assoc_ptr->id);
			} else {
				xstrfmtcat(resv_ptr->assoc_list, ",-%u,",
					   assoc_ptr->id);
			}
		}
		list_iterator_destroy(itr);
	}
	debug("assoc_list:%s", resv_ptr->assoc_list);

end_it:
	FREE_NULL_LIST(assoc_list_allow);
	FREE_NULL_LIST(assoc_list_deny);
	assoc_mgr_unlock(&locks);

	return rc;
}

/* Post reservation create */
static int _post_resv_create(slurmctld_resv_t *resv_ptr)
{
	int rc = SLURM_SUCCESS;
	slurmdb_reservation_rec_t resv;

	_set_boot_time(resv_ptr);

	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT)
		return rc;

	memset(&resv, 0, sizeof(slurmdb_reservation_rec_t));
	resv.assocs = resv_ptr->assoc_list;
	resv.cluster = slurm_conf.cluster_name;
	resv.comment = resv_ptr->comment;
	resv.tres_str = resv_ptr->tres_str;

	resv.flags = resv_ptr->flags;
	resv.id = resv_ptr->resv_id;
	resv.name = resv_ptr->name;
	resv.nodes = resv_ptr->node_list;
	resv.node_inx = acct_storage_g_node_inx(acct_db_conn,
						resv_ptr->node_list);
	resv.time_end = resv_ptr->end_time;
	resv.time_start = resv_ptr->start_time;
	resv.tres_str = resv_ptr->tres_str;

	rc = acct_storage_g_add_reservation(acct_db_conn, &resv);

	xfree(resv.node_inx);

	return rc;
}

/* Note that a reservation has been deleted */
static int _post_resv_delete(slurmctld_resv_t *resv_ptr)
{
	int rc = SLURM_SUCCESS;
	slurmdb_reservation_rec_t resv;
	time_t now = time(NULL);

	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT)
		return rc;

	memset(&resv, 0, sizeof(slurmdb_reservation_rec_t));
	resv.cluster = slurm_conf.cluster_name;
	resv.id = resv_ptr->resv_id;
	resv.name = resv_ptr->name;
	resv.time_end = now;
	resv.time_start = resv_ptr->start_time;
	/* This is just a time stamp here to delete if the reservation
	 * hasn't started yet so we don't get trash records in the
	 * database if said database isn't up right now */
	resv.time_start_prev = now;
	resv.tres_str = resv_ptr->tres_str;
	rc = acct_storage_g_remove_reservation(acct_db_conn, &resv);

	return rc;
}

/* Note that a reservation has been updated */
static int _post_resv_update(slurmctld_resv_t *resv_ptr,
			     slurmctld_resv_t *old_resv_ptr)
{
	int rc = SLURM_SUCCESS;
	bool change = false;
	slurmdb_reservation_rec_t resv;
	time_t now = time(NULL);

	xassert(old_resv_ptr);

	_set_boot_time(resv_ptr);

	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT)
		return rc;

	memset(&resv, 0, sizeof(slurmdb_reservation_rec_t));
	resv.cluster = slurm_conf.cluster_name;
	resv.id = resv_ptr->resv_id;
	resv.time_end = resv_ptr->end_time;
	resv.assocs = resv_ptr->assoc_list;
	resv.tres_str = resv_ptr->tres_str;
	resv.flags = resv_ptr->flags;
	resv.nodes = resv_ptr->node_list;
	resv.comment = resv_ptr->comment;

	if (xstrcmp(old_resv_ptr->assoc_list, resv_ptr->assoc_list) ||
	    xstrcmp(old_resv_ptr->tres_str, resv_ptr->tres_str)	||
	    (old_resv_ptr->flags != resv_ptr->flags) ||
	    xstrcmp(old_resv_ptr->node_list, resv_ptr->node_list) ||
	    xstrcmp(old_resv_ptr->comment, resv_ptr->comment))
		change = true;

	/* Here if the reservation has started already we need
	 * to mark a new start time for it if certain
	 * variables are needed in accounting.  Right now if
	 * the assocs, nodes, flags or cpu count changes we need a
	 * new start time of now. */
	if ((resv_ptr->start_time < now) && change) {
		resv_ptr->start_time_prev = resv_ptr->start_time;
		resv_ptr->start_time = now;
	}

	/* now set the (maybe new) start_times */
	resv.time_start = resv_ptr->start_time;
	resv.time_start_prev = resv_ptr->start_time_prev;

	resv.node_inx = acct_storage_g_node_inx(acct_db_conn,
						resv_ptr->node_list);

	rc = acct_storage_g_modify_reservation(acct_db_conn, &resv);

	xfree(resv.node_inx);

	return rc;
}

static void _remove_name_from_str(char *name, char *str)
{
	int k = strlen(name);
	char *tmp = str, *tok;

	while ((tok = xstrstr(tmp, name))) {
		if (((tok != str) &&
		     (tok[-1] != ',') && (tok[-1] != '-')) ||
		    ((tok[k] != '\0') && (tok[k] != ','))) {
			tmp = tok + 1;
			continue;
		}
		if (tok[-1] == '-') {
			tok--;
			k++;
		}
		if (tok[-1] == ',') {
			tok--;
			k++;
		} else if (tok[k] == ',')
			k++;
		for (int j=0; ; j++) {
			tok[j] = tok[j + k];
			if (tok[j] == '\0')
				break;
		}
	}
}

static bool _check_uid(uid_t x, uid_t arg)
{
	return (x == arg);
}

static bool _check_char(char *x, char *arg)
{
	if (x[0] == '-')
		x++;
	return !xstrcmp(x, arg);
}

static int _handle_add_remove_names(
	slurmctld_resv_t *resv_ptr, uint32_t not_flag,
	int alter_cnt, char **alter_list, uid_t *uid_list, int *alter_types,
	bool minus, bool plus)
{
	int *object_cnt;
	char **object_str;
	int i, j, k;
	bool found_it;

	switch (not_flag) {
	case RESV_CTLD_USER_NOT:
		object_cnt = &resv_ptr->user_cnt;
		object_str = &resv_ptr->users;
		break;
	case RESV_CTLD_ACCT_NOT:
		object_cnt = &resv_ptr->account_cnt;
		object_str = &resv_ptr->accounts;
		break;
	default:
		return SLURM_ERROR;
	}

	/*
	 * If: The update sets a new list (it was previously empty).
	 * All accounts are negated, so this is a new exclusion list.
	 * NOTE: An empty list is always of type "inclusion".
	 */
	if (!*object_cnt && minus && !plus)
		resv_ptr->ctld_flags |= not_flag;

	if (resv_ptr->ctld_flags & not_flag) {
		/*
		 * change minus to plus (add to NOT list) and
		 * change plus to minus (remove from NOT list)
		 */
		for (i = 0; i < alter_cnt; i++) {
			if (alter_types[i] == 1)
				alter_types[i] = 2;
			else if (alter_types[i] == 2)
				alter_types[i] = 1;
		}
		if (minus && !plus) {
			minus = false;
			plus = true;
		} else if (!minus && plus) {
			minus = true;
			plus = false;
		}
	}

	/*
	 * At this point, minus/plus mean removing/adding literally to the list.
	 * If "RESV_CTLD_*_NOT" was previously set, it means the list is of
	 * type "exclusion", otherwise it means "inclusion"
	 */
	if (minus) {
		if (!*object_cnt)
			return SLURM_ERROR;
		for (i=0; i < alter_cnt; i++) {
			if (alter_types[i] != 1) /* not minus */
				continue;
			found_it = false;
			for (j=0; j < *object_cnt; j++) {
				switch (not_flag) {
				case RESV_CTLD_USER_NOT:
					found_it = _check_uid(
						resv_ptr->user_list[j],
						uid_list[i]);
					break;
				case RESV_CTLD_ACCT_NOT:
					found_it = _check_char(
						resv_ptr->account_list[j],
						alter_list[i]);
					break;
				default:
					break;
				}
				if (found_it)
					break;
			}
			if (!found_it)
				return SLURM_ERROR;

			_remove_name_from_str(alter_list[i], *object_str);
			if (!*object_str[0])
				xfree(*object_str);

			(*object_cnt)--;
			for (k=j; k<*object_cnt; k++) {
				switch (not_flag) {
				case RESV_CTLD_USER_NOT:
					resv_ptr->user_list[k] =
						resv_ptr->user_list[k+1];
					break;
				case RESV_CTLD_ACCT_NOT:
					resv_ptr->account_list[k] =
						resv_ptr->account_list[k+1];
					break;
				default:
					break;
				}
			}
		}
	}

	if (plus) {
		for (i=0; i<alter_cnt; i++) {
			if (alter_types[i] != 2) /* not plus */
				continue;
			found_it = false;
			for (j=0; j<*object_cnt; j++) {
				switch (not_flag) {
				case RESV_CTLD_USER_NOT:
					found_it = _check_uid(
						resv_ptr->user_list[j],
						uid_list[i]);
					break;
				case RESV_CTLD_ACCT_NOT:
					found_it = _check_char(
						resv_ptr->account_list[j],
						alter_list[i]);
					break;
				default:
					break;
				}
				if (found_it)
					break;
			}
			if (found_it)
				continue; /* duplicate entry */

			if (*object_str && *object_str[0])
				xstrcat(*object_str, ",");
			if (resv_ptr->ctld_flags & not_flag)
				xstrcat(*object_str, "-");
			xstrcat(*object_str, alter_list[i]);

			switch (not_flag) {
			case RESV_CTLD_USER_NOT:
				xrecalloc(resv_ptr->user_list,
					  (*object_cnt + 1), sizeof(uid_t));
				resv_ptr->user_list[(*object_cnt)++] =
					uid_list[i];
				break;
			case RESV_CTLD_ACCT_NOT:
				xrecalloc(resv_ptr->account_list,
					  (*object_cnt + 1), sizeof(char *));
				resv_ptr->account_list[(*object_cnt)++] =
					xstrdup(alter_list[i]);
				break;
			default:
				break;
			}
		}
	}

	return SLURM_SUCCESS;
}

/*
 * Validate a comma delimited list of account names and build an array of
 *	them
 * IN account       - a list of account names
 * OUT account_cnt  - number of accounts in the list
 * OUT account_list - list of the account names,
 *		      CALLER MUST XFREE this plus each individual record
 * OUT account_not  - true of account_list is that of accounts to be blocked
 *                    from reservation access
 * RETURN 0 on success
 */
static int _build_account_list(char *accounts, int *account_cnt,
			       char ***account_list, bool *account_not)
{
	char *last = NULL, *tmp, *tok;
	int ac_cnt = 0, i;
	char **ac_list;

	*account_cnt = 0;
	*account_list = (char **) NULL;
	*account_not = false;

	if (!accounts)
		return ESLURM_INVALID_ACCOUNT;

	i = strlen(accounts);
	ac_list = xcalloc((i + 2), sizeof(char *));
	tmp = xstrdup(accounts);
	tok = strtok_r(tmp, ",", &last);
	while (tok) {
		if (tok[0] == '-') {
			if (ac_cnt == 0) {
				*account_not = true;
			} else if (*account_not != true) {
				info("Reservation request has some "
				     "not/accounts");
				goto inval;
			}
			tok++;
		} else if (*account_not != false) {
			info("Reservation request has some not/accounts");
			goto inval;
		}
		if (!_is_account_valid(tok)) {
			info("Reservation request has invalid account %s",
			     tok);
			goto inval;
		}
		ac_list[ac_cnt++] = xstrdup(tok);
		tok = strtok_r(NULL, ",", &last);
	}
	*account_cnt  = ac_cnt;
	*account_list = ac_list;
	xfree(tmp);
	return SLURM_SUCCESS;

 inval:	for (i=0; i<ac_cnt; i++)
		xfree(ac_list[i]);
	xfree(ac_list);
	xfree(tmp);
	return ESLURM_INVALID_ACCOUNT;
}

/*
 * Update a account list for an existing reservation based upon an
 *	update comma delimited specification of accounts to add (+name),
 *	remove (-name), or set value of
 * IN/OUT resv_ptr - pointer to reservation structure being updated
 * IN accounts     - a list of account names, to set, add, or remove
 * RETURN 0 on success
 */
static int  _update_account_list(slurmctld_resv_t *resv_ptr,
				 char *accounts)
{
	char *last = NULL, *ac_cpy, *tok;
	int ac_cnt = 0, i, rc = SLURM_ERROR;
	int *ac_type, minus_account = 0, plus_account = 0;
	char **ac_list;

	if (!accounts)
		return ESLURM_INVALID_ACCOUNT;

	i = strlen(accounts);
	ac_list = xcalloc((i + 2), sizeof(char *));
	ac_type = xcalloc((i + 2), sizeof(int));
	ac_cpy = xstrdup(accounts);
	tok = strtok_r(ac_cpy, ",", &last);
	while (tok) {
		if (tok[0] == '-') {
			ac_type[ac_cnt] = 1;	/* minus */
			minus_account = 1;
			tok++;
		} else if (tok[0] == '+') {
			ac_type[ac_cnt] = 2;	/* plus */
			plus_account = 1;
			tok++;
		} else if (tok[0] == '\0') {
			continue;
		} else if (plus_account || minus_account) {
			info("Reservation account expression invalid %s",
			     accounts);
			goto inval;
		} else
			ac_type[ac_cnt] = 3;	/* set */
		if (!_is_account_valid(tok)) {
			info("Reservation request has invalid account %s",
			     tok);
			goto inval;
		}
		ac_list[ac_cnt++] = xstrdup(tok);
		tok = strtok_r(NULL, ",", &last);
	}

	if ((plus_account == 0) && (minus_account == 0)) {
		/* Just a reset of account list */
		xfree(resv_ptr->accounts);
		if (accounts[0] != '\0')
			resv_ptr->accounts = xstrdup(accounts);
		for (i = 0; i < resv_ptr->account_cnt; i++)
			xfree(resv_ptr->account_list[i]);
		xfree(resv_ptr->account_list);
		resv_ptr->account_list = ac_list;
		resv_ptr->account_cnt  = ac_cnt;
		resv_ptr->ctld_flags &= (~RESV_CTLD_ACCT_NOT);
		xfree(ac_cpy);
		xfree(ac_type);
		return SLURM_SUCCESS;
	}

	rc = _handle_add_remove_names(resv_ptr, RESV_CTLD_ACCT_NOT,
				      ac_cnt, ac_list, NULL, ac_type,
				      minus_account, plus_account);

inval:
	for (i=0; i<ac_cnt; i++)
		xfree(ac_list[i]);
	xfree(ac_list);
	xfree(ac_type);
	xfree(ac_cpy);

	if (rc != SLURM_SUCCESS)
		rc = ESLURM_INVALID_ACCOUNT;
	return rc;
}

/*
 * Validate a comma delimited list of user names and build an array of
 *	their UIDs
 * IN users      - a list of user names
 * OUT user_cnt  - number of UIDs in the list
 * OUT user_list - list of the user's uid, CALLER MUST XFREE;
 * OUT user_not  - true of user_list is that of users to be blocked
 *                 from reservation access
 * IN strict     - true if an invalid user invalidates the reservation
 * RETURN 0 on success
 */
static int _build_uid_list(char *users, int *user_cnt, uid_t **user_list,
			   bool *user_not, bool strict)
{
	char *last = NULL, *tmp = NULL, *tok;
	int u_cnt = 0, i;
	uid_t *u_list, u_tmp;

	*user_cnt = 0;
	*user_list = (uid_t *) NULL;
	*user_not = false;

	if (!users)
		return ESLURM_USER_ID_MISSING;

	i = strlen(users);
	u_list = xcalloc((i + 2), sizeof(uid_t));
	tmp = xstrdup(users);
	tok = strtok_r(tmp, ",", &last);
	while (tok) {
		if (tok[0] == '-') {
			if (u_cnt == 0) {
				*user_not = true;
			} else if (*user_not != true) {
				info("Reservation request has some not/users");
				goto inval;
			}
			tok++;
		} else if (*user_not != false) {
			info("Reservation request has some not/users");
			goto inval;
		}
		if (uid_from_string (tok, &u_tmp) < 0) {
			info("Reservation request has invalid user %s", tok);
			if (strict)
				goto inval;
		} else {
			u_list[u_cnt++] = u_tmp;
		}
		tok = strtok_r(NULL, ",", &last);
	}
	if (u_cnt > 0) {
		*user_cnt  = u_cnt;
		*user_list = u_list;
		xfree(tmp);
		return SLURM_SUCCESS;
	} else {
		info("Reservation request has no valid users");
	}

 inval:	xfree(tmp);
	xfree(u_list);
	return ESLURM_USER_ID_MISSING;
}

/*
 * Update a user/uid list for an existing reservation based upon an
 *	update comma delimited specification of users to add (+name),
 *	remove (-name), or set value of
 * IN/OUT resv_ptr - pointer to reservation structure being updated
 * IN users        - a list of user names, to set, add, or remove
 * RETURN 0 on success
 */
static int _update_uid_list(slurmctld_resv_t *resv_ptr, char *users)
{
	char *last = NULL, *u_cpy = NULL, *tok;
	int u_cnt = 0, i;
	uid_t *u_list, u_tmp;
	int *u_type, minus_user = 0, plus_user = 0, rc = ESLURM_USER_ID_MISSING;
	char **u_name;

	if (!users)
		return ESLURM_USER_ID_MISSING;

	/* Parse the incoming user expression */
	i = strlen(users);
	u_list = xcalloc((i + 2), sizeof(uid_t));
	u_name = xcalloc((i + 2), sizeof(char *));
	u_type = xcalloc((i + 2), sizeof(int));
	u_cpy = xstrdup(users);
	tok = strtok_r(u_cpy, ",", &last);
	while (tok) {
		if (tok[0] == '-') {
			u_type[u_cnt] = 1;	/* minus */
			minus_user = 1;
			tok++;
		} else if (tok[0] == '+') {
			u_type[u_cnt] = 2;	/* plus */
			plus_user = 1;
			tok++;
		} else if (tok[0] == '\0') {
			continue;
		} else if (plus_user || minus_user) {
			info("Reservation user expression invalid %s", users);
			goto inval;
		} else
			u_type[u_cnt] = 3;	/* set */

		if (uid_from_string (tok, &u_tmp) < 0) {
			info("Reservation request has invalid user %s", tok);
			goto inval;
		}

		u_name[u_cnt] = tok;
		u_list[u_cnt++] = u_tmp;
		tok = strtok_r(NULL, ",", &last);
	}

	if ((plus_user == 0) && (minus_user == 0)) {
		/* Just a reset of user list */
		xfree(resv_ptr->users);
		xfree(resv_ptr->user_list);
		if (users[0] != '\0')
			resv_ptr->users = xstrdup(users);
		resv_ptr->user_cnt  = u_cnt;
		resv_ptr->user_list = u_list;
		resv_ptr->ctld_flags &= (~RESV_CTLD_USER_NOT);
		xfree(u_cpy);
		xfree(u_name);
		xfree(u_type);
		return SLURM_SUCCESS;
	}

	rc = _handle_add_remove_names(resv_ptr, RESV_CTLD_USER_NOT,
				      u_cnt, u_name, u_list, u_type,
				      minus_user, plus_user);
inval:
	xfree(u_cpy);
	xfree(u_list);
	xfree(u_name);
	xfree(u_type);

	if (rc != SLURM_SUCCESS)
		rc = ESLURM_USER_ID_MISSING;
	return rc;
}

/*
 * Update a group/uid list for an existing reservation based upon an
 *	update comma delimited specification of groups to add (+name),
 *	remove (-name), or set value of
 * IN/OUT resv_ptr - pointer to reservation structure being updated
 * IN groups        - a list of user names, to set, add, or remove
 * RETURN 0 on success
 */
static int _update_group_uid_list(slurmctld_resv_t *resv_ptr, char *groups)
{
	char *last = NULL, *g_cpy = NULL, *tok, *tok2, *resv_groups = NULL;
	bool plus = false, minus = false;

	if (!groups)
		return ESLURM_GROUP_ID_MISSING;

	/* Parse the incoming group expression */
	g_cpy = xstrdup(groups);
	tok = strtok_r(g_cpy, ",", &last);
	if (tok)
		resv_groups = xstrdup(resv_ptr->groups);

	while (tok) {
		if (tok[0] == '-') {
			char *tmp = resv_groups;
			int k;

			tok++;
			/* Now we need to remove from groups string */
			k = strlen(tok);
			while ((tok2 = xstrstr(tmp, tok))) {
				if (((tok2 != resv_groups) &&
				     (tok2[-1] != ',') && (tok2[-1] != '-')) ||
				    ((tok2[k] != '\0') && (tok2[k] != ','))) {
					tmp = tok2 + 1;
					continue;
				}
				if (tok2[-1] == '-') {
					tok2--;
					k++;
				}
				if (tok2[-1] == ',') {
					tok2--;
					k++;
				} else if (tok2[k] == ',')
					k++;
				for (int j=0; ; j++) {
					tok2[j] = tok2[j+k];
					if (tok2[j] == '\0')
						break;
				}
			}
			minus = true;
		} else if (tok[0] == '+') {
			tok++;
			if ((tok2 = xstrstr(resv_groups, tok)))
				continue;

			xstrfmtcat(resv_groups, "%s%s",
				   resv_groups ? "," : "",
				   tok);
			plus = true;
		} else if (tok[0] == '\0') {
			continue;
		} else if (plus || minus) {
			info("Reservation group expression invalid %s", groups);
			goto inval;
		} else {
			/*
			 * It is a straight list set the pointers right and
			 * break
			 */
			xfree(resv_groups);
			resv_groups = xstrdup(groups);
			break;
		}
		tok = strtok_r(NULL, ",", &last);
	}

	/* Just a reset of group list */
	resv_ptr->ctld_flags &= (~RESV_CTLD_USER_NOT);

	xfree(resv_ptr->groups);
	xfree(resv_ptr->user_list);
	resv_ptr->user_cnt = 0;

	if (resv_groups && resv_groups[0] != '\0') {
		resv_ptr->user_list =
			get_groups_members(resv_groups, &resv_ptr->user_cnt);

		if (resv_ptr->user_cnt) {
			resv_ptr->groups = resv_groups;
			resv_groups = NULL;
		} else {
			goto inval;
		}
	}

	xfree(g_cpy);
	xfree(resv_groups);
	return SLURM_SUCCESS;

inval:
	xfree(g_cpy);
	xfree(resv_groups);
	return ESLURM_GROUP_ID_MISSING;
}

/* Given a core_resrcs data structure (which has information only about the
 * nodes in that reservation), build a global core_bitmap (which includes
 * information about all nodes in the system).
 * RET SLURM_SUCCESS or error code */
static int _get_core_resrcs(slurmctld_resv_t *resv_ptr)
{
	int i, j, node_inx;
	int c, core_offset_local, core_offset_global, core_end, core_set;
	node_record_t *node_ptr;

	if (!resv_ptr->core_resrcs || resv_ptr->core_bitmap ||
	    !resv_ptr->core_resrcs->core_bitmap ||
	    (bit_ffs(resv_ptr->core_resrcs->core_bitmap) == -1))
		return SLURM_SUCCESS;

	FREE_NULL_BITMAP(resv_ptr->core_resrcs->node_bitmap);
	if (resv_ptr->core_resrcs->nodes &&
	    (node_name2bitmap(resv_ptr->core_resrcs->nodes, false,
			      &resv_ptr->core_resrcs->node_bitmap, NULL))) {
		error("Invalid nodes (%s) for reservation %s",
		      resv_ptr->core_resrcs->nodes, resv_ptr->name);
		return SLURM_ERROR;
	} else if (resv_ptr->core_resrcs->nodes == NULL) {
		resv_ptr->core_resrcs->node_bitmap =
			bit_alloc(node_record_count);
	}

	i = bit_set_count(resv_ptr->core_resrcs->node_bitmap);
	if (resv_ptr->core_resrcs->nhosts != i) {
		error("Invalid change in resource allocation node count for "
		      "reservation %s, %u to %d",
		      resv_ptr->name, resv_ptr->core_resrcs->nhosts, i);
		return SLURM_ERROR;
	}

	node_conf_create_cluster_core_bitmap(&resv_ptr->core_bitmap);
	for (i = 0, node_inx = -1;
	     (node_ptr = next_node_bitmap(resv_ptr->core_resrcs->node_bitmap,
					  &i));
	     i++) {
		node_inx++;
		core_offset_global = cr_get_coremap_offset(i);
		core_end = cr_get_coremap_offset(i + 1);
		core_offset_local = get_job_resources_offset(
					resv_ptr->core_resrcs, node_inx, 0, 0);
		core_set = 0;
		for (c = core_offset_global, j = core_offset_local;
		     c < core_end &&
		     core_set < resv_ptr->core_resrcs->cpus[node_inx];
		     c++, j++) {
			if (!bit_test(resv_ptr->core_resrcs->core_bitmap, j))
				continue;
			bit_set(resv_ptr->core_bitmap, c);
			core_set += node_ptr->threads;
		}
		if (core_set < resv_ptr->core_resrcs->cpus[node_inx]) {
			error("Unable to restore reservation %s on node_inx %d of nodes %s. Probably node configuration changed",
			      resv_ptr->name, node_inx,
			      resv_ptr->core_resrcs->nodes);
			return SLURM_ERROR;
		}
	}

	return SLURM_SUCCESS;
}

/*
 * _pack_resv - dump configuration information about a specific reservation
 *	in machine independent form (for network transmission or state save)
 * IN resv_ptr - pointer to reservation for which information is requested
 * IN/OUT buffer - buffer in which data is placed, pointers automatically
 *	updated
 * IN internal   - true if for internal save state, false for xmit to users
 * NOTE: if you make any changes here be sure to make the corresponding
 *	to _unpack_reserve_info_members() in common/slurm_protocol_pack.c
 *	plus load_all_resv_state() below.
 */
static void _pack_resv(slurmctld_resv_t *resv_ptr, buf_t *buffer,
		       bool internal, uint16_t protocol_version)
{
	time_t now = time(NULL), start_relative, end_relative;
	int offset_start, offset_end;
	uint32_t i_cnt;
	node_record_t *node_ptr;
	job_resources_t *core_resrcs;
	char *core_str;

	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT)
		last_resv_update = now;
	if (!internal && (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT)) {
		start_relative = resv_ptr->start_time + now;
		if (resv_ptr->duration == INFINITE)
			end_relative = start_relative + YEAR_SECONDS;
		else if (resv_ptr->duration && (resv_ptr->duration != NO_VAL))
			end_relative = start_relative + resv_ptr->duration * 60;
		else {
			end_relative = resv_ptr->end_time;
			if (start_relative > end_relative)
				start_relative = end_relative;
		}
	} else {
		start_relative = resv_ptr->start_time_first;
		end_relative = resv_ptr->end_time;
	}

	if (protocol_version >= SLURM_23_11_PROTOCOL_VERSION) {
		packstr(resv_ptr->accounts,	buffer);
		packstr(resv_ptr->burst_buffer,	buffer);
		packstr(resv_ptr->comment,	buffer);
		pack32(resv_ptr->core_cnt,	buffer);
		pack_time(end_relative,		buffer);
		packstr(resv_ptr->features,	buffer);
		pack64(resv_ptr->flags,		buffer);
		packstr(resv_ptr->licenses,	buffer);
		pack32(resv_ptr->max_start_delay, buffer);
		packstr(resv_ptr->name,		buffer);
		pack32(resv_ptr->node_cnt,	buffer);
		packstr(resv_ptr->node_list,	buffer);
		packstr(resv_ptr->partition,	buffer);
		pack32(resv_ptr->purge_comp_time, buffer);
		pack32(NO_VAL, buffer); /* was resv_watts */
		pack_time(start_relative,	buffer);
		packstr(resv_ptr->tres_fmt_str,	buffer);
		packstr(resv_ptr->users,	buffer);
		packstr(resv_ptr->groups, buffer);

		if (internal) {
			packstr(resv_ptr->assoc_list,	buffer);
			pack32(resv_ptr->boot_time,	buffer);
			/*
			 * NOTE: Restoring core_bitmap directly only works if
			 * the system's node and core counts don't change.
			 * core_resrcs is used so configuration changes can be
			 * supported
			 */
			pack_job_resources(resv_ptr->core_resrcs, buffer,
					   protocol_version);
			pack32(resv_ptr->duration,	buffer);
			pack32(resv_ptr->resv_id,	buffer);
			pack_time(resv_ptr->start_time_prev, buffer);
			pack_time(resv_ptr->start_time,	buffer);
			pack_time(resv_ptr->idle_start_time, buffer);
			packstr(resv_ptr->tres_str,	buffer);
			pack32(resv_ptr->ctld_flags,	buffer);
			(void) gres_job_state_pack(resv_ptr->gres_list_alloc,
						   buffer, 0,
						   false,
						   protocol_version);
		} else {
			pack_bit_str_hex(resv_ptr->node_bitmap, buffer);
			if (!resv_ptr->core_bitmap ||
			    !resv_ptr->core_resrcs ||
			    !resv_ptr->core_resrcs->node_bitmap ||
			    !resv_ptr->core_resrcs->core_bitmap ||
			    (bit_ffs(resv_ptr->core_bitmap) == -1)) {
				pack32((uint32_t) 0, buffer);
			} else {
				core_resrcs = resv_ptr->core_resrcs;
				i_cnt = bit_set_count(core_resrcs->node_bitmap);
				pack32(i_cnt, buffer);
				for (int i = 0;
				     (node_ptr =
				      next_node_bitmap(core_resrcs->node_bitmap,
						       &i));
				     i++) {
					offset_start = cr_get_coremap_offset(i);
					offset_end = cr_get_coremap_offset(i+1);
					packstr(node_ptr->name, buffer);
					core_str = bit_fmt_range(
						resv_ptr->core_bitmap,
						offset_start,
						(offset_end - offset_start));
					packstr(core_str, buffer);
					xfree(core_str);
				}
			}
		}
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(resv_ptr->accounts,	buffer);
		packstr(resv_ptr->burst_buffer,	buffer);
		packstr(resv_ptr->comment,	buffer);
		pack32(resv_ptr->core_cnt,	buffer);
		pack_time(end_relative,		buffer);
		packstr(resv_ptr->features,	buffer);
		pack64(resv_ptr->flags,		buffer);
		packstr(resv_ptr->licenses,	buffer);
		pack32(resv_ptr->max_start_delay, buffer);
		packstr(resv_ptr->name,		buffer);
		pack32(resv_ptr->node_cnt,	buffer);
		packstr(resv_ptr->node_list,	buffer);
		packstr(resv_ptr->partition,	buffer);
		pack32(resv_ptr->purge_comp_time, buffer);
		pack32(NO_VAL, buffer); /* was resv_watts */
		pack_time(start_relative,	buffer);
		packstr(resv_ptr->tres_fmt_str,	buffer);
		packstr(resv_ptr->users,	buffer);
		packstr(resv_ptr->groups, buffer);

		if (internal) {
			packstr(resv_ptr->assoc_list,	buffer);
			pack32(resv_ptr->boot_time,	buffer);
			/*
			 * NOTE: Restoring core_bitmap directly only works if
			 * the system's node and core counts don't change.
			 * core_resrcs is used so configuration changes can be
			 * supported
			 */
			pack_job_resources(resv_ptr->core_resrcs, buffer,
					   protocol_version);
			pack32(resv_ptr->duration,	buffer);
			pack32(resv_ptr->resv_id,	buffer);
			pack_time(resv_ptr->start_time_prev, buffer);
			pack_time(resv_ptr->start_time,	buffer);
			pack_time(resv_ptr->idle_start_time, buffer);
			packstr(resv_ptr->tres_str,	buffer);
			pack32(resv_ptr->ctld_flags,	buffer);
		} else {
			pack_bit_str_hex(resv_ptr->node_bitmap, buffer);
			if (!resv_ptr->core_bitmap ||
			    !resv_ptr->core_resrcs ||
			    !resv_ptr->core_resrcs->node_bitmap ||
			    !resv_ptr->core_resrcs->core_bitmap ||
			    (bit_ffs(resv_ptr->core_bitmap) == -1)) {
				pack32((uint32_t) 0, buffer);
			} else {
				core_resrcs = resv_ptr->core_resrcs;
				i_cnt = bit_set_count(core_resrcs->node_bitmap);
				pack32(i_cnt, buffer);
				for (int i = 0;
				     (node_ptr =
				      next_node_bitmap(core_resrcs->node_bitmap,
						       &i));
				     i++) {
					offset_start = cr_get_coremap_offset(i);
					offset_end = cr_get_coremap_offset(i+1);
					packstr(node_ptr->name, buffer);
					core_str = bit_fmt_range(
						resv_ptr->core_bitmap,
						offset_start,
						(offset_end - offset_start));
					packstr(core_str, buffer);
					xfree(core_str);
				}
			}
		}
	}
}

slurmctld_resv_t *_load_reservation_state(buf_t *buffer,
					  uint16_t protocol_version)
{
	slurmctld_resv_t *resv_ptr;

	resv_ptr = xmalloc(sizeof(slurmctld_resv_t));
	resv_ptr->magic = RESV_MAGIC;
	if (protocol_version >= SLURM_23_11_PROTOCOL_VERSION) {
		uint32_t uint32_tmp;
		safe_unpackstr(&resv_ptr->accounts, buffer);
		safe_unpackstr(&resv_ptr->burst_buffer, buffer);
		safe_unpackstr(&resv_ptr->comment, buffer);
		safe_unpack32(&resv_ptr->core_cnt,	buffer);
		safe_unpack_time(&resv_ptr->end_time,	buffer);
		safe_unpackstr(&resv_ptr->features, buffer);
		safe_unpack64(&resv_ptr->flags,		buffer);
		safe_unpackstr(&resv_ptr->licenses, buffer);
		safe_unpack32(&resv_ptr->max_start_delay, buffer);
		safe_unpackstr(&resv_ptr->name,	buffer);

		safe_unpack32(&resv_ptr->node_cnt,	buffer);
		safe_unpackstr(&resv_ptr->node_list, buffer);
		safe_unpackstr(&resv_ptr->partition, buffer);
		safe_unpack32(&resv_ptr->purge_comp_time, buffer);
		safe_unpack32(&uint32_tmp, buffer); /* was resv_watts */
		safe_unpack_time(&resv_ptr->start_time_first,	buffer);
		safe_unpackstr(&resv_ptr->tres_fmt_str, buffer);
		safe_unpackstr(&resv_ptr->users, buffer);
		safe_unpackstr(&resv_ptr->groups, buffer);

		/* Fields saved for internal use only (save state) */
		safe_unpackstr(&resv_ptr->assoc_list, buffer);
		safe_unpack32(&resv_ptr->boot_time,	buffer);
		if (unpack_job_resources(&resv_ptr->core_resrcs, buffer,
					 protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&resv_ptr->duration,	buffer);
		safe_unpack32(&resv_ptr->resv_id,	buffer);
		safe_unpack_time(&resv_ptr->start_time_prev, buffer);
		safe_unpack_time(&resv_ptr->start_time, buffer);
		safe_unpack_time(&resv_ptr->idle_start_time, buffer);
		safe_unpackstr(&resv_ptr->tres_str, buffer);
		safe_unpack32(&resv_ptr->ctld_flags, buffer);
		if (gres_job_state_unpack(&resv_ptr->gres_list_alloc, buffer,
					  0, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		gres_job_state_log(resv_ptr->gres_list_alloc, 0);
		if (!resv_ptr->purge_comp_time)
			resv_ptr->purge_comp_time = 300;
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint32_t uint32_tmp;
		safe_unpackstr(&resv_ptr->accounts, buffer);
		safe_unpackstr(&resv_ptr->burst_buffer, buffer);
		safe_unpackstr(&resv_ptr->comment, buffer);
		safe_unpack32(&resv_ptr->core_cnt,	buffer);
		safe_unpack_time(&resv_ptr->end_time,	buffer);
		safe_unpackstr(&resv_ptr->features, buffer);
		safe_unpack64(&resv_ptr->flags,		buffer);
		safe_unpackstr(&resv_ptr->licenses, buffer);
		safe_unpack32(&resv_ptr->max_start_delay, buffer);
		safe_unpackstr(&resv_ptr->name,	buffer);

		safe_unpack32(&resv_ptr->node_cnt,	buffer);
		safe_unpackstr(&resv_ptr->node_list, buffer);
		safe_unpackstr(&resv_ptr->partition, buffer);
		safe_unpack32(&resv_ptr->purge_comp_time, buffer);
		safe_unpack32(&uint32_tmp, buffer); /* was resv_watts */
		safe_unpack_time(&resv_ptr->start_time_first,	buffer);
		safe_unpackstr(&resv_ptr->tres_fmt_str, buffer);
		safe_unpackstr(&resv_ptr->users, buffer);
		safe_unpackstr(&resv_ptr->groups, buffer);

		/* Fields saved for internal use only (save state) */
		safe_unpackstr(&resv_ptr->assoc_list, buffer);
		safe_unpack32(&resv_ptr->boot_time,	buffer);
		if (unpack_job_resources(&resv_ptr->core_resrcs, buffer,
					 protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&resv_ptr->duration,	buffer);
		safe_unpack32(&resv_ptr->resv_id,	buffer);
		safe_unpack_time(&resv_ptr->start_time_prev, buffer);
		safe_unpack_time(&resv_ptr->start_time, buffer);
		safe_unpack_time(&resv_ptr->idle_start_time, buffer);
		safe_unpackstr(&resv_ptr->tres_str, buffer);
		safe_unpack32(&resv_ptr->ctld_flags, buffer);
		if (!resv_ptr->purge_comp_time)
			resv_ptr->purge_comp_time = 300;
	} else
		goto unpack_error;

	return resv_ptr;

unpack_error:
	error("Incomplete reservation state save file");
	_del_resv_rec(resv_ptr);
	return NULL;
}

/*
 * Test if a new/updated reservation request will overlap running jobs
 * Ignore jobs already running in that specific reservation
 * resv_name IN - Name of existing reservation or NULL
 * RET true if overlap
 */
static bool _job_overlap(time_t start_time, uint64_t flags,
			 bitstr_t *node_bitmap, char *resv_name)
{
	list_itr_t *job_iterator;
	job_record_t *job_ptr;
	bool overlap = false;

	if (!node_bitmap ||			/* No nodes to test for */
	    (flags & RESERVE_FLAG_IGN_JOBS))	/* ignore job overlap */
		return overlap;
	if (flags & RESERVE_FLAG_TIME_FLOAT)
		start_time += time(NULL);

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (IS_JOB_RUNNING(job_ptr)		&&
		    (job_ptr->end_time > start_time)	&&
		    bit_overlap_any(job_ptr->node_bitmap, node_bitmap) &&
		    ((resv_name == NULL) ||
		     (xstrcmp(resv_name, job_ptr->resv_name) != 0))) {
			overlap = true;
			break;
		}
	}
	list_iterator_destroy(job_iterator);

	return overlap;
}

/*
 * Test if a new/updated reservation request overlaps an existing
 *	reservation
 * RET true if overlap
 */
static bool _resv_overlap(resv_desc_msg_t *resv_desc_ptr,
			  bitstr_t *node_bitmap,
			  slurmctld_resv_t *this_resv_ptr)
{
	list_itr_t *iter;
	slurmctld_resv_t *resv_ptr;
	bool rc = false;

	if ((resv_desc_ptr->flags & RESERVE_FLAG_MAINT)   ||
	    (resv_desc_ptr->flags & RESERVE_FLAG_OVERLAP) ||
	    (!node_bitmap))
		return rc;

	iter = list_iterator_create(resv_list);

	while ((resv_ptr = list_next(iter))) {
		if (resv_ptr == this_resv_ptr)
			continue;	/* skip self */
		if (resv_ptr->node_bitmap == NULL)
			continue;	/* no specific nodes in reservation */
		if ((resv_ptr->flags & RESERVE_FLAG_MAINT) ||
		    (resv_ptr->flags & RESERVE_FLAG_OVERLAP))
			continue;
		if (!bit_overlap_any(resv_ptr->node_bitmap, node_bitmap))
			continue;	/* no overlap */
		if (!(resv_ptr->ctld_flags & RESV_CTLD_FULL_NODE))
			continue;
		if (_resv_time_overlap(resv_desc_ptr, resv_ptr)) {
			rc = true;
			break;
		}
	}
	list_iterator_destroy(iter);

	return rc;
}

static bool _slots_overlap(const constraint_slot_t *slot0,
			   const constraint_slot_t *slot1)
{
	if ((slot0->start < slot1->end) &&
	    (slot1->start < slot0->end))
		return true;

	return false;
}

/*
 * Get number of seconds to next reoccurring time slot.
 *
 * Used to check overlapping time slots. See _advance_time() for actual
 * advancement of reservation.
 */
static time_t _get_advance_secs(const constraint_slot_t *slot)
{
	time_t reoccurring_secs = -1;
	struct tm tm;

	if (slot->flags & RESERVE_FLAG_WEEKDAY) {
		localtime_r(&(slot->start), &tm);
		if (tm.tm_wday == 5)		/* Friday */
			reoccurring_secs = 60 * 60 * 24 * 3;
		else if (tm.tm_wday == 6)	/* Saturday */
			reoccurring_secs = 60 * 60 * 24 * 2;
		else
			reoccurring_secs = 60 * 60 * 24;
	} else if (slot->flags & RESERVE_FLAG_WEEKEND) {
		localtime_r(&(slot->start), &tm);
		if (tm.tm_wday == 6)	/* Saturday */
			reoccurring_secs = 60 * 60 * 24;
		else
			reoccurring_secs = 60 * 60 * 24 * (6 - tm.tm_wday);
	} else if (slot->flags & RESERVE_FLAG_WEEKLY) {
		reoccurring_secs = 60 * 60 * 24 * 7;
	} else if (slot->flags & RESERVE_FLAG_DAILY) {
		reoccurring_secs = 60 * 60 * 24;
	} else if (slot->flags & RESERVE_FLAG_HOURLY) {
		reoccurring_secs = 60 * 60;
	} else {
		error("%s: Unknown recurring reservation flags",
		      __func__);
		return -1;
	}

	return reoccurring_secs;
}

static void _advance_slot(constraint_slot_t *slot)
{
	time_t reoccurring_secs = 0;

	if (!slot) {
		error("%s: Reservation slot is NULL and it shouldn't happen",
		      __func__);
		return;
	}

	if (!(slot->flags & RESERVE_REOCCURRING))
		return;

	if ((reoccurring_secs = _get_advance_secs(slot)) == -1)
		return;

	slot->start += reoccurring_secs;
	slot->end += reoccurring_secs;
}

static void _advance_slot_until(constraint_slot_t *slot, time_t end)
{
	constraint_slot_t slot_advanced;
	time_t reoccurring_secs = 0;
	int reoccurrings = 0;

	if (!slot) {
		error("%s: Reservation slot is NULL and it shouldn't happen",
		      __func__);
		return;
	}

	if (!(slot->flags & RESERVE_REOCCURRING))
		return;

	if (slot->start > end) {
		error("%s: Reservation slot starts after the requested end this shouldn't happen",
		      __func__);
		return;
	}

	if (slot->flags & (RESERVE_FLAG_WEEKDAY | RESERVE_FLAG_WEEKEND)) {
		slot_advanced = *slot;
		while (slot_advanced.start < end) {
			*slot = slot_advanced;
			_advance_slot(&slot_advanced);
		}
	} else {
		/* Avoid while loop for regular reoccurrings for performance */
		if ((reoccurring_secs = _get_advance_secs(slot)) == -1)
			return;

		/*
		 * As reoccurrings is a truncated integer we ensure that
		 * slot->start will be <= end-1 (ie, < end).
		 */
		reoccurrings = (end - 1 - slot->start) / reoccurring_secs;
		slot->start += reoccurrings * reoccurring_secs;
		slot->end += reoccurrings * reoccurring_secs;

		if (reoccurrings < 0)
			error("%s: Number of reoccurrings for the reservation slot is negative and this shouldn't happen",
			      __func__);
	}
}

static bool _resv_time_overlap(resv_desc_msg_t *resv_desc_ptr,
			       slurmctld_resv_t *resv_ptr)
{
	time_t now = time(NULL);
	constraint_slot_t slot_a = {
		.start = resv_desc_ptr->start_time,
		.end = resv_desc_ptr->end_time,
		.duration = resv_desc_ptr->duration,
		.flags = resv_desc_ptr->flags,
	};
	constraint_slot_t slot_b = {
		.start = resv_ptr->start_time,
		.end = resv_ptr->end_time,
		.duration = resv_ptr->duration,
		.flags = resv_ptr->flags,
	};

	constraint_slot_t *slot[2] = {&slot_a, &slot_b};

	for (int i = 0; i < 2; i++) {
		/* Set real start/end for floating reservations */
		if (slot[i]->flags & RESERVE_FLAG_TIME_FLOAT) {
			slot[i]->start += now;
			if (slot[i]->duration == INFINITE)
				slot[i]->end = slot[i]->start + YEAR_SECONDS;
			else if (slot[i]->duration &&
				 (slot[i]->duration != NO_VAL))
				slot[i]->end = slot[i]->start +
					slot[i]->duration * 60;
		}

		/* Sanity check */
		if (slot[i]->start > slot[i]->end) {
			error("%s: Reservation slot has start > end and it shouldn't happen",
				__func__);
			return true;
		}
	}

	/* Ensure that slot0 is the earlier resv and slot1 the latter one */
	if (slot[1]->end < slot[0]->end) {
		slot[0] = &slot_b;
		slot[1] = &slot_a;
	}

	/* Check base overlapping */
	if (_slots_overlap(slot[0], slot[1])) {
		log_flag(RESERVATION, "%s: Reservation slots overlap",
			 __func__);
		return true;
	}

	/*
	 * Handling reoccurring slots
	 * 1) Advance the earlier slot to the closest period to the end of the
	 *    latter one, while keeping the order between them.
	 *    Check if slot0 reservation's end overlaps start time of slot1.
	 * 2) Advance slot0 one more time, so it's now the latter.
	 *    Check if slot0 reservation's start overlaps end time of slot1.
	 * 3) If slot1 is also recurring, repeat 1) switching slot0 and slot1.
	 *
	 * e.g.
	 *   slot0 reoccurs WEEKLY on Mondays at 5:00 for 1h starting today.
	 *   slot1 reoccurs HOURLY for 30m starting next Tuesday at 4:00.
	 *
	 *   1) slot0 is advanced to next Monday and won't overlap.
	 *   2) slot0 is advanced to next next Monday and won't overlap.
	 *   3) slot1 is advanced HOURLY until it reaches next next Monday and
	 *      will overlap.
	 */
	if (slot[0]->flags & RESERVE_REOCCURRING) {
		/*
		 * 1) Advance earlier slot to the last reoccurring period
		 *    before the later slot ends.
		 */
		_advance_slot_until(slot[0], slot[1]->end);
		if (slot[0]->end > slot[1]->end) {
			error("%s: Reservation slot is already the last one, and it shouldn't happen",
			      __func__);
			return true;
		}

		/* Check overlap in the original order */
		if (_slots_overlap(slot[0], slot[1])) {
			log_flag(RESERVATION, "%s: Reservation slots overlap due reoccurrings of the earlier reservation",
				 __func__);
			return true;
		}

		/*
		 * 2) Advance earlier slot once to convert it into the later one
		 */
		_advance_slot(slot[0]);
		if (slot[0]->end < slot[1]->end) {
			error("%s: Reservation slot is still the first one, and it shouldn't happen",
			      __func__);
			return true;
		}

		/*
		 * Check overlap after slot0 is now the latter reservation
		 * Check if slot0 reservation's start overlaps end time of slot1.
		 */
		if (_slots_overlap(slot[0], slot[1])) {
			log_flag(RESERVATION, "%s: Reservation slots overlap due reocurrings of the earlier reservation, once it becomes the later one",
				 __func__);
			return true;
		}

		if (slot[1]->flags & RESERVE_REOCCURRING) {
			/* 3) Repeat 1) with slot1 being the earlier one */
			_advance_slot_until(slot[1], slot[0]->end);
			if (slot[1]->end > slot[0]->end) {
				error("%s: Reservation slot is the later one again, and it shouldn't happen",
				      __func__);
				return true;
			}

			/* Check overlap */
			if (_slots_overlap(slot[0], slot[1])) {
				log_flag(RESERVATION, "%s: Reservations overlap due recurrence of the later reservation",
					 __func__);
				return true;
			}
		}
	}

	return false;
}

/* Set a reservation's TRES count. Requires that the reservation's
 *	node_bitmap be set.
 * This needs to be done after all other setup is done.
 */
static void _set_tres_cnt(slurmctld_resv_t *resv_ptr,
			  slurmctld_resv_t *old_resv_ptr)
{
	uint64_t cpu_cnt = 0;
	node_record_t *node_ptr;
	char start_time[256], end_time[256], tmp_msd[40];
	char *name1, *name2, *name3, *val1, *val2, *val3;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	if ((resv_ptr->ctld_flags & RESV_CTLD_FULL_NODE) &&
	    resv_ptr->node_bitmap) {
		resv_ptr->core_cnt = 0;

		for (int i = 0;
		     (node_ptr = next_node_bitmap(resv_ptr->node_bitmap, &i));
		     i++) {
			resv_ptr->core_cnt += node_ptr->tot_cores;
			cpu_cnt += node_ptr->cpus;
		}
	} else if (resv_ptr->core_bitmap) {
		resv_ptr->core_cnt =
			bit_set_count(resv_ptr->core_bitmap);
		cpu_cnt = resv_ptr->core_resrcs->ncpus;
	}

	xfree(resv_ptr->tres_str);
	if (resv_ptr->gres_list_alloc) { /* First, doesn't add comma */
		assoc_mgr_lock_t locks = { .tres = READ_LOCK };
		uint64_t *tres_alloc_cnt;

		assoc_mgr_lock(&locks);
		tres_alloc_cnt = xcalloc(slurmctld_tres_cnt, sizeof(uint64_t));
		gres_stepmgr_set_job_tres_cnt(
			resv_ptr->gres_list_alloc,
			resv_ptr->node_cnt,
			tres_alloc_cnt,
			true);
		resv_ptr->tres_str = assoc_mgr_make_tres_str_from_array(
			tres_alloc_cnt, TRES_STR_FLAG_SIMPLE, true);
		xfree(tres_alloc_cnt);
		assoc_mgr_unlock(&locks);
	}

	if (cpu_cnt)
		xstrfmtcat(resv_ptr->tres_str, "%s%u=%"PRIu64,
			   resv_ptr->tres_str ? "," : "",
			   TRES_CPU, cpu_cnt);

	if ((name1 = licenses_2_tres_str(resv_ptr->license_list))) {
		xstrfmtcat(resv_ptr->tres_str, "%s%s",
			   resv_ptr->tres_str ? "," : "",
			   name1);
		xfree(name1);
	}

	if ((name1 = bb_g_xlate_bb_2_tres_str(resv_ptr->burst_buffer))) {
		xstrfmtcat(resv_ptr->tres_str, "%s%s",
			   resv_ptr->tres_str ? "," : "",
			   name1);
		xfree(name1);
	}

	xfree(resv_ptr->tres_fmt_str);
	assoc_mgr_lock(&locks);
	resv_ptr->tres_fmt_str = slurmdb_make_tres_string_from_simple(
		resv_ptr->tres_str, assoc_mgr_tres_list, NO_VAL,
		CONVERT_NUM_UNIT_EXACT, 0, NULL);
	assoc_mgr_unlock(&locks);

	slurm_make_time_str(&resv_ptr->start_time, start_time,
			    sizeof(start_time));
	slurm_make_time_str(&resv_ptr->end_time, end_time, sizeof(end_time));
	if (resv_ptr->accounts) {
		name1 = " accounts=";
		val1  = resv_ptr->accounts;
	} else
		name1 = val1 = "";
	if (resv_ptr->users) {
		name2 = " users=";
		val2  = resv_ptr->users;
	} else
		name2 = val2 = "";

	if (resv_ptr->groups) {
		name3 = " groups=";
		val3  = resv_ptr->groups;
	} else
		name3 = val3 = "";

	if (resv_ptr->max_start_delay)
		secs2time_str(resv_ptr->max_start_delay,
			      tmp_msd, sizeof(tmp_msd));

	sched_info("%s reservation=%s%s%s%s%s%s%s nodes=%s cores=%u "
		   "licenses=%s tres=%s start=%s end=%s MaxStartDelay=%s "
		   "Comment=%s",
		   old_resv_ptr ? "Updated" : "Created",
		   resv_ptr->name, name1, val1, name2, val2, name3, val3,
		   resv_ptr->node_list, resv_ptr->core_cnt, resv_ptr->licenses,
		   resv_ptr->tres_fmt_str,
		   start_time, end_time,
		   resv_ptr->max_start_delay ? tmp_msd : "",
		   resv_ptr->comment ? resv_ptr->comment : "");
	if (old_resv_ptr)
		_post_resv_update(resv_ptr, old_resv_ptr);
	else
		_post_resv_create(resv_ptr);
}

/*
 * _license_validate2 - A variant of license_validate which considers the
 * licenses used by overlapping reservations
 */
static list_t *_license_validate2(resv_desc_msg_t *resv_desc_ptr, bool *valid)
{
	list_t *license_list = NULL, *merged_list = NULL;
	list_itr_t *iter;
	slurmctld_resv_t *resv_ptr;
	char *merged_licenses;

	license_list = license_validate(resv_desc_ptr->licenses, true, true,
					NULL, valid);
	if (resv_desc_ptr->licenses == NULL)
		return license_list;

	merged_licenses = xstrdup(resv_desc_ptr->licenses);
	iter = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(iter))) {
		if ((resv_ptr->licenses   == NULL) ||
		    (resv_ptr->end_time   <= resv_desc_ptr->start_time) ||
		    (resv_ptr->start_time >= resv_desc_ptr->end_time))
			continue;	/* No overlap */
		if (resv_desc_ptr->name &&
		    !xstrcmp(resv_desc_ptr->name, resv_ptr->name))
			continue;	/* Modifying this reservation */
		xstrcat(merged_licenses, ",");
		xstrcat(merged_licenses, resv_ptr->licenses);
	}
	list_iterator_destroy(iter);
	merged_list = license_validate(merged_licenses, true, true, NULL,
				       valid);
	xfree(merged_licenses);
	FREE_NULL_LIST(merged_list);
	return license_list;
}

static int _delete_resv_internal(slurmctld_resv_t *resv_ptr)
{
	if (_is_resv_used(resv_ptr))
		return ESLURM_RESERVATION_BUSY;

	if (resv_ptr->ctld_flags & RESV_CTLD_NODE_FLAGS_SET) {
		time_t now = time(NULL);
		resv_ptr->ctld_flags &= (~RESV_CTLD_NODE_FLAGS_SET);
		_set_nodes_flags(resv_ptr, now,
				 (NODE_STATE_RES | NODE_STATE_MAINT),
				 false);
		last_node_update = now;
	}

	return _post_resv_delete(resv_ptr);
}

static bitstr_t *_get_update_node_bitmap(slurmctld_resv_t *resv_ptr,
					 char *node_list)
{
	char *last = NULL, *tmp, *tok, *node_name;
	node_record_t *node_ptr;
	bitstr_t *node_bitmap = NULL;
	hostlist_t *hl = NULL;

	tmp = xstrdup(node_list);
	tok = node_conf_nodestr_tokenize(tmp, &last);
	while (tok) {
		bool minus = false, plus = false;
		if (tok[0] == '-') {
			minus = true;
			tok++;
		} else if (tok[0] == '+') {
			plus = true;
			tok++;
		} else if (tok[0] == '\0') {
			break;
		}

		if (!plus && !minus) {
			if (node_bitmap) {
				info("Reservation %s request has bad nodelist given (%s)",
				     resv_ptr->name, node_list);
				FREE_NULL_BITMAP(node_bitmap);
			} else
				(void)node_name2bitmap(node_list, false,
						       &node_bitmap, NULL);
			break;
		}

		/* Create hostlist to handle ranges i.e. tux[0-10] */
		hl = hostlist_create(tok);
		while ((node_name = hostlist_shift(hl))) {
			node_ptr = find_node_record(node_name);
			if (!node_ptr) {
				info("Reservation %s request has bad node name given (%s)",
				     resv_ptr->name, node_name);
				free(node_name);
				FREE_NULL_BITMAP(node_bitmap);
				break;
			}
			free(node_name);

			if (!node_bitmap)
				node_bitmap = bit_copy(resv_ptr->node_bitmap);

			if (plus)
				bit_set(node_bitmap, node_ptr->index);
			else if (minus)
				bit_clear(node_bitmap, node_ptr->index);
		}
		hostlist_destroy(hl);

		if (!node_bitmap)
			break;

		tok = node_conf_nodestr_tokenize(NULL, &last);
	}
	xfree(tmp);

	return node_bitmap;
}

/* Returns false if only one reoccurring flag is set, true otherwise */
static bool _has_multiple_reoccurring(resv_desc_msg_t *resv_desc_ptr){
	int flag_count = 0;
	if (resv_desc_ptr->flags & RESERVE_FLAG_HOURLY)
		flag_count++;
	if (resv_desc_ptr->flags & RESERVE_FLAG_DAILY)
		flag_count++;
	if (resv_desc_ptr->flags & RESERVE_FLAG_WEEKDAY)
		flag_count++;
	if (resv_desc_ptr->flags & RESERVE_FLAG_WEEKEND)
		flag_count++;
	if (resv_desc_ptr->flags & RESERVE_FLAG_WEEKLY)
		flag_count++;

	return (flag_count > 1);
}

static void _set_tres_err_msg(char **err_msg, int rc)
{
	if (!err_msg)
		return;

	switch (rc) {
	case ESLURM_INVALID_BURST_BUFFER_REQUEST:
		*err_msg = xstrdup("TRES=<buffer_spec>=<num> and BurstBuffer=<buffer_spec> are mutually exclusive");
		break;
	case ESLURM_INVALID_CPU_COUNT:
		*err_msg = xstrdup("TRES=cpu=<num> and CoreCnt=<num> are mutually exclusive");
		break;
	case ESLURM_INVALID_LICENSES:
		*err_msg = xstrdup("TRES=license/<name>=<num> and Licenses=<name>:<num> are mutually exclusive");
		break;
	case ESLURM_INVALID_NODE_COUNT:
		*err_msg = xstrdup("TRES=node=<num> and Nodes=<num> are mutually exclusive");
		break;
	}
}

/* Create a resource reservation */
extern int create_resv(resv_desc_msg_t *resv_desc_ptr, char **err_msg)
{
	int i, rc = SLURM_SUCCESS;
	time_t now = time(NULL);
	part_record_t *part_ptr = NULL;
	slurmctld_resv_t *resv_ptr = NULL;
	int account_cnt = 0, user_cnt = 0;
	char **account_list = NULL;
	uid_t *user_list = NULL;
	list_t *license_list = NULL;
	uint32_t total_node_cnt = 0;
	bool account_not = false, user_not = false;
	resv_select_t resv_select = { 0 };

	_create_resv_lists(false);

	if (resv_desc_ptr->flags == NO_VAL64)
		resv_desc_ptr->flags = 0;
	else {
		resv_desc_ptr->flags &= RESERVE_FLAG_MAINT    |
					RESERVE_FLAG_FLEX     |
					RESERVE_FLAG_OVERLAP  |
					RESERVE_FLAG_IGN_JOBS |
					RESERVE_FLAG_HOURLY   |
					RESERVE_FLAG_DAILY    |
					RESERVE_FLAG_WEEKDAY  |
					RESERVE_FLAG_WEEKEND  |
					RESERVE_FLAG_WEEKLY   |
					RESERVE_FLAG_STATIC   |
					RESERVE_FLAG_ANY_NODES   |
					RESERVE_FLAG_PART_NODES  |
					RESERVE_FLAG_TIME_FLOAT  |
					RESERVE_FLAG_PURGE_COMP  |
					RESERVE_FLAG_REPLACE     |
					RESERVE_FLAG_REPLACE_DOWN |
					RESERVE_FLAG_NO_HOLD_JOBS |
					RESERVE_FLAG_MAGNETIC |
					RESERVE_FLAG_USER_DEL |
					RESERVE_TRES_PER_NODE;
	}

	if ((rc = _parse_tres_str(resv_desc_ptr)) != SLURM_SUCCESS) {
		_set_tres_err_msg(err_msg, rc);
		return rc;
	}

	_dump_resv_req(resv_desc_ptr, "create_resv");

	if (xstrcasestr(resv_desc_ptr->tres_str, "gres"))
		resv_desc_ptr->flags |= RESERVE_FLAG_GRES_REQ;

	/* Validate the request */
	if ((resv_desc_ptr->core_cnt != NO_VAL) && !slurm_select_cr_type()) {
		char *err_str = "CoreCnt only supported with cons_tres.";
		info("%s", err_str);
		if (err_msg)
			*err_msg = xstrdup(err_str);
		rc = ESLURM_NOT_SUPPORTED;
		goto bad_parse;
	}

	if (resv_desc_ptr->start_time != (time_t) NO_VAL) {
		if (resv_desc_ptr->flags & RESERVE_FLAG_TIME_FLOAT) {
			if (resv_desc_ptr->start_time < now)
				resv_desc_ptr->start_time = now;
		} else if (resv_desc_ptr->start_time < (now - MAX_RESV_DELAY)) {
			info("Reservation request has invalid start time");
			rc = ESLURM_INVALID_TIME_VALUE;
			goto bad_parse;
		}
	} else
		resv_desc_ptr->start_time = now;

	if (resv_desc_ptr->end_time != (time_t) NO_VAL) {
		if (resv_desc_ptr->end_time < (now - MAX_RESV_DELAY)) {
			info("Reservation request has invalid end time");
			rc = ESLURM_INVALID_TIME_VALUE;
			goto bad_parse;
		}
	} else if (resv_desc_ptr->duration == INFINITE) {
		resv_desc_ptr->end_time = resv_desc_ptr->start_time +
					  YEAR_SECONDS;
	} else if (resv_desc_ptr->duration) {
		resv_desc_ptr->end_time = resv_desc_ptr->start_time +
					  (resv_desc_ptr->duration * 60);
	} else
		resv_desc_ptr->end_time = INFINITE;

	if (resv_desc_ptr->flags & RESERVE_REOCCURRING) {
		if (_has_multiple_reoccurring(resv_desc_ptr)) {
			info("Reservation has multiple reoccurring flags. Please specify only one reoccurring flag");
			if (err_msg)
				*err_msg = xstrdup("Reservation has multiple reoccurring flags. Please specify only one reoccurring flag");
			rc = ESLURM_NOT_SUPPORTED;
			goto bad_parse;
		}
	}

	if ((resv_desc_ptr->flags & RESERVE_FLAG_REPLACE) ||
	    (resv_desc_ptr->flags & RESERVE_FLAG_REPLACE_DOWN)) {
		if (resv_desc_ptr->node_list) {
			info("%s: REPLACE or REPLACE_DOWN flags should be used with the NodeCnt reservation option; do not specify Nodes",
				__func__);
			if (err_msg)
				*err_msg = xstrdup("REPLACE or REPLACE_DOWN flags should be used with the NodeCnt reservation option; do not specify Nodes");
			rc = ESLURM_INVALID_NODE_NAME;
			goto bad_parse;
		}
		if (resv_desc_ptr->core_cnt != NO_VAL) {
			info("%s: REPLACE or REPLACE_DOWN flags should be used with the NodeCnt reservation option; do not specify CoreCnt",
				__func__);
			if (err_msg)
				*err_msg = xstrdup("REPLACE or REPLACE_DOWN flags should be used with the NodeCnt reservation option; do not specify CoreCnt");
			rc = ESLURM_INVALID_CPU_COUNT;
			goto bad_parse;
		}
	}

	if (((resv_desc_ptr->flags & RESERVE_FLAG_REPLACE) ||
	     (resv_desc_ptr->flags & RESERVE_FLAG_REPLACE_DOWN)) &&
	    ((resv_desc_ptr->flags & RESERVE_FLAG_STATIC) ||
	     (resv_desc_ptr->flags & RESERVE_FLAG_MAINT))) {
		info("REPLACE and REPLACE_DOWN flags cannot be used with STATIC_ALLOC or MAINT flags");
		if (err_msg)
			*err_msg = xstrdup("REPLACE and REPLACE_DOWN flags cannot be used with STATIC_ALLOC or MAINT flags");
		rc = ESLURM_NOT_SUPPORTED;
		goto bad_parse;
	}

	if (resv_desc_ptr->partition) {
		part_ptr = find_part_record(resv_desc_ptr->partition);
		if (!part_ptr) {
			info("Reservation request has invalid partition %s",
			     resv_desc_ptr->partition);
			rc = ESLURM_INVALID_PARTITION_NAME;
			goto bad_parse;
		}
	} else if (resv_desc_ptr->flags & RESERVE_FLAG_PART_NODES) {
		info("Reservation request with Part_Nodes flag lacks "
		     "partition specification");
		rc = ESLURM_INVALID_PARTITION_NAME;
		goto bad_parse;
	}

	if ((resv_desc_ptr->flags & RESERVE_FLAG_PART_NODES) &&
	    (xstrcasecmp(resv_desc_ptr->node_list, "ALL"))) {
		info("Reservation request with Part_Nodes flag lacks nodelist=ALL specification");
		rc = ESLURM_INVALID_NODE_NAME;
		goto bad_parse;
	}

	if (resv_desc_ptr->users && resv_desc_ptr->groups) {
		info("Reservation request with both users and groups, these are mutually exclusive.  You can have one or the other, but not both.");
		rc = ESLURM_RESERVATION_USER_GROUP;
		goto bad_parse;

	} else if (!resv_desc_ptr->accounts &&
	    !resv_desc_ptr->users &&
	    !resv_desc_ptr->groups) {
		info("Reservation request lacks users, accounts or groups");
		rc = ESLURM_RESERVATION_EMPTY;
		goto bad_parse;
	}

	if (resv_desc_ptr->accounts) {
		rc = _build_account_list(resv_desc_ptr->accounts,
					 &account_cnt, &account_list,
					 &account_not);
		if (rc)
			goto bad_parse;
	}
	if (resv_desc_ptr->users) {
		rc = _build_uid_list(resv_desc_ptr->users,
				     &user_cnt, &user_list, &user_not, true);
		if (rc)
			goto bad_parse;
	}

	if (resv_desc_ptr->groups) {
		user_list =
			get_groups_members(resv_desc_ptr->groups, &user_cnt);

		if (!user_list) {
			rc = ESLURM_GROUP_ID_MISSING;
			goto bad_parse;
		}
		info("processed groups %s", resv_desc_ptr->groups);
	}

	if (resv_desc_ptr->licenses) {
		bool valid = true;
		license_list = _license_validate2(resv_desc_ptr, &valid);
		if (!valid) {
			info("Reservation request has invalid licenses %s",
			     resv_desc_ptr->licenses);
			rc = ESLURM_INVALID_LICENSES;
			goto bad_parse;
		}
	}
	if ((resv_desc_ptr->flags & RESERVE_FLAG_TIME_FLOAT) &&
	    (resv_desc_ptr->flags & RESERVE_REOCCURRING)) {
		info("Reservation request has mutually exclusive flags. Repeating floating reservations are not supported.");
		if (err_msg)
			*err_msg = xstrdup("Reservation request has mutually exclusive flags. Repeating floating reservations are not supported.");
		rc = ESLURM_NOT_SUPPORTED;
		goto bad_parse;
	}

	if (resv_desc_ptr->node_list) {
		resv_desc_ptr->flags |= RESERVE_FLAG_SPEC_NODES;
		if (xstrcasecmp(resv_desc_ptr->node_list, "ALL") == 0) {
			if (resv_desc_ptr->partition && part_ptr &&
			    (resv_desc_ptr->flags & RESERVE_FLAG_PART_NODES)) {
				resv_select.node_bitmap =
					bit_copy(part_ptr->node_bitmap);
			} else {
				resv_desc_ptr->flags &=
					(~RESERVE_FLAG_PART_NODES);
				resv_desc_ptr->flags |= RESERVE_FLAG_ALL_NODES;
				resv_select.node_bitmap =
					node_conf_get_active_bitmap();
			}
			xfree(resv_desc_ptr->node_list);
			resv_desc_ptr->node_list =
				bitmap2node_name(resv_select.node_bitmap);
		} else {
			resv_desc_ptr->flags &= (~RESERVE_FLAG_PART_NODES);
			if (node_name2bitmap(resv_desc_ptr->node_list, false,
					     &resv_select.node_bitmap, NULL)) {
				rc = ESLURM_INVALID_NODE_NAME;
				goto bad_parse;
			}
			xfree(resv_desc_ptr->node_list);
			resv_desc_ptr->node_list = bitmap2node_name(resv_select.node_bitmap);
		}
		if (bit_set_count(resv_select.node_bitmap) == 0) {
			info("Reservation node list is empty");
			rc = ESLURM_INVALID_NODE_NAME;
			goto bad_parse;
		}
		if (!(resv_desc_ptr->flags & RESERVE_FLAG_OVERLAP) &&
		    _resv_overlap(resv_desc_ptr, resv_select.node_bitmap, NULL)) {
			info("Reservation request overlaps another");
			rc = ESLURM_RESERVATION_OVERLAP;
			goto bad_parse;
		}
		total_node_cnt = bit_set_count(resv_select.node_bitmap);
		if ((resv_desc_ptr->node_cnt == NO_VAL) ||
		    (resv_desc_ptr->node_cnt < total_node_cnt)) {
			resv_desc_ptr->node_cnt = total_node_cnt;
			if ((resv_desc_ptr->flags & RESERVE_TRES_PER_NODE) &&
			    (resv_desc_ptr->core_cnt != NO_VAL))
				resv_desc_ptr->core_cnt *=
					resv_desc_ptr->node_cnt;
		}
		if (!(resv_desc_ptr->flags & RESERVE_FLAG_IGN_JOBS) &&
		    (resv_desc_ptr->core_cnt == NO_VAL)) {
			uint64_t flags = resv_desc_ptr->flags;

			/*
			 * Need to clear this flag before _job_overlap()
			 * which would otherwise add the current time
			 * on to the start_time. start_time for floating
			 * reservations has already been set to now.
			 */
			flags &= ~RESERVE_FLAG_TIME_FLOAT;

			if (_job_overlap(resv_desc_ptr->start_time, flags,
					 resv_select.node_bitmap, NULL)) {
				info("Reservation request overlaps jobs");
				rc = ESLURM_NODES_BUSY;
				goto bad_parse;
			}
		}
		/* We do allow to request cores with nodelist */
		if ((resv_desc_ptr->flags & RESERVE_FLAG_GRES_REQ) ||
		    (resv_desc_ptr->core_cnt != NO_VAL)) {
			if (!resv_desc_ptr->core_cnt) {
				info("Core count for reservation nodelist is not consistent!");
				rc = ESLURM_INVALID_CORE_CNT;
				goto bad_parse;
			}
			if (resv_desc_ptr->flags & RESERVE_FLAG_GRES_REQ)
				log_flag(RESERVATION, "%s: Requesting TRES/GRES '%s' for node_list",
					 __func__, resv_desc_ptr->tres_str);
			else
				log_flag(RESERVATION, "%s: Requesting %d cores for node_list",
					 __func__,
					 resv_desc_ptr->core_cnt);
			resv_desc_ptr->job_ptr =
				job_mgr_copy_resv_desc_to_job_record(
					resv_desc_ptr);
			rc = _select_nodes(resv_desc_ptr, &part_ptr,
					   &resv_select);
			if (rc != SLURM_SUCCESS)
				goto bad_parse;
		}
	} else if (!(resv_desc_ptr->flags & RESERVE_FLAG_ANY_NODES)) {
		resv_desc_ptr->flags &= (~RESERVE_FLAG_PART_NODES);

		if ((resv_desc_ptr->node_cnt == NO_VAL) &&
		    (resv_desc_ptr->core_cnt == NO_VAL) &&
		    !(resv_desc_ptr->flags & RESERVE_FLAG_GRES_REQ)) {
			info("Reservation request lacks node specification");
			rc = ESLURM_INVALID_NODE_NAME;
		} else {
			resv_desc_ptr->job_ptr =
				job_mgr_copy_resv_desc_to_job_record(
					resv_desc_ptr);
			rc = _select_nodes(resv_desc_ptr, &part_ptr,
					   &resv_select);
		}
		if (rc != SLURM_SUCCESS) {
			goto bad_parse;
		}

		/* Get count of allocated nodes, on BlueGene systems, this
		 * might be more than requested */
		total_node_cnt = bit_set_count(resv_select.node_bitmap);
	}

	if ((resv_desc_ptr->core_cnt != NO_VAL) && !resv_select.core_bitmap) {
		info("Attempt to reserve cores not possible with current "
		     "configuration");
		rc = ESLURM_INVALID_CPU_COUNT;
		goto bad_parse;
	}

	/*
	 * A reservation without nodes/cores should only be possible if the flag
	 * ANY_NODES is set and it has at least one of licenses or burst buffer.
	 * So test this here after the checks for the involved options.
	 */
	if ((resv_desc_ptr->flags & RESERVE_FLAG_ANY_NODES) &&
	    !total_node_cnt && !resv_select.core_bitmap && !resv_desc_ptr->burst_buffer &&
	    (!license_list || list_is_empty(license_list)) &&
	    !resv_desc_ptr->tres_str) {
		info("%s: reservations without nodes and with ANY_NODES flag are expected to be one of Licenses, BurstBuffer, and/or TRES", __func__);
		rc = ESLURM_RESERVATION_INVALID;
		goto bad_parse;
	}

	rc = _generate_resv_id();
	if (rc != SLURM_SUCCESS)
		goto bad_parse;

	/* If name == NULL or empty string, then generate a name. */
	if (resv_desc_ptr->name && (resv_desc_ptr->name[0] != '\0')) {
		resv_ptr = find_resv_name(resv_desc_ptr->name);
		if (resv_ptr) {
			info("Reservation request name duplication (%s)",
			     resv_desc_ptr->name);
			rc = ESLURM_RESERVATION_NAME_DUP;
			goto bad_parse;
		}
	} else {
		xfree(resv_desc_ptr->name);
		while (1) {
			_generate_resv_name(resv_desc_ptr);
			resv_ptr = find_resv_name(resv_desc_ptr->name);
			if (!resv_ptr)
				break;
			rc = _generate_resv_id();	/* makes new suffix */
			if (rc != SLURM_SUCCESS)
				goto bad_parse;
			/* Same as previously created name, retry */
		}
	}

	/* Create a new reservation record */
	resv_ptr = xmalloc(sizeof(slurmctld_resv_t));
	resv_ptr->magic = RESV_MAGIC;
	resv_ptr->accounts	= resv_desc_ptr->accounts;
	resv_desc_ptr->accounts = NULL;		/* Nothing left to free */
	resv_ptr->account_cnt	= account_cnt;
	resv_ptr->account_list	= account_list;
	account_cnt = 0;
	account_list = NULL;
	resv_ptr->burst_buffer	= resv_desc_ptr->burst_buffer;
	resv_desc_ptr->burst_buffer = NULL;	/* Nothing left to free */
	resv_ptr->comment = resv_desc_ptr->comment;
	resv_desc_ptr->comment = NULL;		/* Nothing left to free */

	if (resv_desc_ptr->job_ptr) {
		job_record_t *job_ptr = resv_desc_ptr->job_ptr;
		resv_ptr->core_resrcs = job_ptr->job_resrcs;
		job_ptr->job_resrcs = NULL; /* Nothing left to free */
		resv_ptr->gres_list_alloc = job_ptr->gres_list_req;
		gres_job_state_log(resv_ptr->gres_list_alloc, 0);
		job_ptr->gres_list_req = NULL; /* Nothing left to free */
		job_record_delete(resv_desc_ptr->job_ptr);
		resv_desc_ptr->job_ptr = NULL; /* Nothing left to free */
	}

	if (user_not)
		resv_ptr->ctld_flags |= RESV_CTLD_USER_NOT;
	if (account_not)
		resv_ptr->ctld_flags |= RESV_CTLD_ACCT_NOT;

	resv_ptr->duration      = resv_desc_ptr->duration;
	if (resv_desc_ptr->purge_comp_time != NO_VAL)
		resv_ptr->purge_comp_time = resv_desc_ptr->purge_comp_time;
	else
		resv_ptr->purge_comp_time = 300; /* default to 5 minutes */
	resv_ptr->end_time	= resv_desc_ptr->end_time;
	resv_ptr->features	= resv_desc_ptr->features;
	resv_desc_ptr->features = NULL;		/* Nothing left to free */
	resv_ptr->licenses	= resv_desc_ptr->licenses;
	resv_desc_ptr->licenses = NULL;		/* Nothing left to free */
	resv_ptr->license_list	= license_list;
	license_list = NULL;

	if (resv_desc_ptr->max_start_delay != NO_VAL)
		resv_ptr->max_start_delay = resv_desc_ptr->max_start_delay;

	resv_ptr->resv_id       = top_suffix;
	resv_ptr->name		= xstrdup(resv_desc_ptr->name);
	resv_ptr->node_cnt	= total_node_cnt;
	resv_ptr->node_list	= resv_desc_ptr->node_list;
	resv_desc_ptr->node_list = NULL;	/* Nothing left to free */
	resv_ptr->node_bitmap	= resv_select.node_bitmap; /* May be unset */
	resv_select.node_bitmap = NULL;
	resv_ptr->core_bitmap	= resv_select.core_bitmap; /* May be unset */
	resv_select.core_bitmap = NULL;
	resv_ptr->partition	= resv_desc_ptr->partition;
	resv_desc_ptr->partition = NULL;	/* Nothing left to free */
	resv_ptr->part_ptr	= part_ptr;
	resv_ptr->start_time	= resv_desc_ptr->start_time;
	resv_ptr->start_time_first = resv_ptr->start_time;
	resv_ptr->start_time_prev = resv_ptr->start_time;
	resv_ptr->flags		= resv_desc_ptr->flags;
	resv_ptr->users		= resv_desc_ptr->users;
	resv_desc_ptr->users 	= NULL;		/* Nothing left to free */
	resv_ptr->groups = resv_desc_ptr->groups;
	resv_desc_ptr->groups = NULL;
	resv_ptr->user_cnt	= user_cnt;
	resv_ptr->user_list	= user_list;
	user_list = NULL;

	if (!(resv_desc_ptr->flags & RESERVE_FLAG_GRES_REQ) &&
	    (resv_desc_ptr->core_cnt == NO_VAL)) {
		log_flag(RESERVATION, "%s: reservation %s using full nodes",
			 __func__, resv_ptr->name);
		resv_ptr->ctld_flags |= RESV_CTLD_FULL_NODE;
	} else {
		log_flag(RESERVATION, "%s: reservation %s using partial nodes",
			 __func__, resv_ptr->name);
		resv_ptr->ctld_flags &= (~RESV_CTLD_FULL_NODE);
	}

	if ((rc = _set_assoc_list(resv_ptr)) != SLURM_SUCCESS) {
		_del_resv_rec(resv_ptr);
		goto bad_parse;
	}

	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT)
		resv_ptr->start_time -= now;

	_set_tres_cnt(resv_ptr, NULL);

	_add_resv_to_lists(resv_ptr);
	last_resv_update = now;
	schedule_resv_save();

	return SLURM_SUCCESS;

 bad_parse:
	job_record_delete(resv_desc_ptr->job_ptr);
	resv_desc_ptr->job_ptr = NULL;
	for (i = 0; i < account_cnt; i++)
		xfree(account_list[i]);
	xfree(account_list);
	FREE_NULL_LIST(license_list);
	_free_resv_select_members(&resv_select);
	xfree(user_list);
	return rc;
}

/* Purge all reservation data structures */
extern void resv_fini(void)
{
	FREE_NULL_LIST(magnetic_resv_list);
	FREE_NULL_LIST(resv_list);
}

static int _validate_reservation_access_update(void *x, void *y)
{
	bool job_use_reservation = false;
	job_record_t *job_ptr = (job_record_t *) x;
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) y;

	if (job_ptr->resv_name == NULL)
		return 0;

	if (IS_JOB_RUNNING(job_ptr) && !xstrcmp(job_ptr->resv_name,
						resv_ptr->name)) {
		job_use_reservation = true;
	} else if (IS_JOB_PENDING(job_ptr) && job_ptr->resv_list &&
		   list_find_first(job_ptr->resv_list, _find_resv_name,
				   resv_ptr->name)) {
		job_use_reservation = true;
	} else if (IS_JOB_PENDING(job_ptr) &&
		   !xstrcmp(job_ptr->resv_name, resv_ptr->name)) {
		job_use_reservation = true;
	}

	if (!job_use_reservation)
		return 0;

	if (_valid_job_access_resv(job_ptr, resv_ptr, false) != SLURM_SUCCESS) {
		info("Rejecting update of reservation %s, because it's in use by %pJ",
		     resv_ptr->name, job_ptr);
		return 1;
	}
	return 0;
}

static int _validate_and_set_partition(part_record_t **part_ptr,
				       char **partition)
{
	if (*part_ptr == NULL) {
		*part_ptr = default_part_loc;
		if (*part_ptr == NULL)
			return ESLURM_DEFAULT_PARTITION_NOT_SET;
	}
	xfree(*partition);
	*partition = xstrdup((*part_ptr)->name);
	return SLURM_SUCCESS;
}

/* Update an exiting resource reservation */
extern int update_resv(resv_desc_msg_t *resv_desc_ptr, char **err_msg)
{
	time_t now = time(NULL);
	slurmctld_resv_t *resv_backup, *resv_ptr;
	resv_desc_msg_t resv_desc;
	int error_code = SLURM_SUCCESS, rc;
	bool skip_it = false;
	bool append_magnetic_resv = false, remove_magnetic_resv = false;
	job_record_t *job_ptr;

	if ((rc = _parse_tres_str(resv_desc_ptr)) != SLURM_SUCCESS) {
		_set_tres_err_msg(err_msg, rc);
		return rc;
	}

	_create_resv_lists(false);
	_dump_resv_req(resv_desc_ptr, "update_resv");

	/* Find the specified reservation */
	if (!resv_desc_ptr->name)
		return ESLURM_RESERVATION_INVALID;

	resv_ptr = find_resv_name(resv_desc_ptr->name);
	if (!resv_ptr)
		return ESLURM_RESERVATION_INVALID;

	if ((resv_desc_ptr->core_cnt != NO_VAL) && !slurm_select_cr_type()) {
		char *err_str = "CoreCnt only supported with cons_tres.";
		info("%s", err_str);
		if (err_msg)
			*err_msg = xstrdup(err_str);
		return ESLURM_NOT_SUPPORTED;
	}

	/* FIXME: Support more core based reservation updates */
	if ((!(resv_ptr->ctld_flags & RESV_CTLD_FULL_NODE) &&
	     ((resv_desc_ptr->node_cnt != NO_VAL) ||
	      resv_desc_ptr->node_list)) ||
	    (resv_desc_ptr->core_cnt != NO_VAL)) {
		char *err_str = "Updating core/node TRES not supported for core-based reservations";
		info("%s(%s): %s", __func__, resv_desc_ptr->name, err_str);
		if (err_msg)
			*err_msg = xstrdup(err_str);
		return ESLURM_CORE_RESERVATION_UPDATE;
	}

	/* Make backup to restore state in case of failure */
	resv_backup = _copy_resv(resv_ptr);

	/* Process the request */
	if (resv_desc_ptr->flags != NO_VAL64) {
		if (resv_desc_ptr->flags & RESERVE_FLAG_FLEX)
			resv_ptr->flags |= RESERVE_FLAG_FLEX;
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_FLEX)
			resv_ptr->flags &= (~RESERVE_FLAG_FLEX);
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_MAINT)
			resv_ptr->flags &= (~RESERVE_FLAG_MAINT);
		if (resv_desc_ptr->flags & RESERVE_FLAG_OVERLAP)
			resv_ptr->flags |= RESERVE_FLAG_OVERLAP;
		if (resv_desc_ptr->flags & RESERVE_FLAG_IGN_JOBS)
			resv_ptr->flags |= RESERVE_FLAG_IGN_JOBS;
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_IGN_JOB)
			resv_ptr->flags &= (~RESERVE_FLAG_IGN_JOBS);
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_HOURLY)
			resv_ptr->flags &= (~RESERVE_FLAG_HOURLY);
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_DAILY)
			resv_ptr->flags &= (~RESERVE_FLAG_DAILY);
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_WEEKDAY)
			resv_ptr->flags &= (~RESERVE_FLAG_WEEKDAY);
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_WEEKEND)
			resv_ptr->flags &= (~RESERVE_FLAG_WEEKEND);
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_WEEKLY)
			resv_ptr->flags &= (~RESERVE_FLAG_WEEKLY);
		if (resv_desc_ptr->flags & RESERVE_FLAG_ANY_NODES)
			resv_ptr->flags |= RESERVE_FLAG_ANY_NODES;
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_ANY_NODES)
			resv_ptr->flags &= (~RESERVE_FLAG_ANY_NODES);
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_STATIC)
			resv_ptr->flags &= (~RESERVE_FLAG_STATIC);
		if (resv_desc_ptr->flags & RESERVE_REOCCURRING) {

			if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT) {
				info("Cannot add a reoccurring flag to a floating reservation");
				if (err_msg)
					*err_msg = xstrdup("Cannot add a reoccurring flag to a floating reservation");
				error_code = ESLURM_NOT_SUPPORTED;
				goto update_failure;
			}

			/*
			 * If the reservation already has a reoccurring flag
			 * that differs from the requested one, or is being
			 * updated to have multiple reoccurring flags, then
			 * reject the update
			 */
			if (((resv_ptr->flags & RESERVE_REOCCURRING) &&
			     ((resv_ptr->flags & RESERVE_REOCCURRING) !=
			      (resv_desc_ptr->flags & RESERVE_REOCCURRING))) ||
			    (_has_multiple_reoccurring(resv_desc_ptr))) {
				info("Cannot update reservation to have multiple reoccurring flags. Please specify only one reoccurring flag");
				if (err_msg)
					*err_msg = xstrdup("Cannot update reservation to have multiple reoccurring flags. Please specify only one reoccurring flag");
				error_code = ESLURM_NOT_SUPPORTED;
				goto update_failure;
			}
			else if (resv_desc_ptr->flags & RESERVE_FLAG_HOURLY)
				resv_ptr->flags |= RESERVE_FLAG_HOURLY;
			else if (resv_desc_ptr->flags & RESERVE_FLAG_DAILY)
				resv_ptr->flags |= RESERVE_FLAG_DAILY;
			else if (resv_desc_ptr->flags & RESERVE_FLAG_WEEKDAY)
				resv_ptr->flags |= RESERVE_FLAG_WEEKDAY;
			else if (resv_desc_ptr->flags & RESERVE_FLAG_WEEKEND)
				resv_ptr->flags |= RESERVE_FLAG_WEEKEND;
			else if (resv_desc_ptr->flags & RESERVE_FLAG_WEEKLY)
				resv_ptr->flags |= RESERVE_FLAG_WEEKLY;
		}
		if ((resv_desc_ptr->flags & RESERVE_FLAG_REPLACE) ||
		    (resv_desc_ptr->flags & RESERVE_FLAG_REPLACE_DOWN)) {
			if ((resv_ptr->flags & RESERVE_FLAG_SPEC_NODES) ||
			    !(resv_ptr->ctld_flags & RESV_CTLD_FULL_NODE)) {
				info("%s: reservation %s can't be updated with REPLACE or REPLACE_DOWN flags; they should be updated on a NodeCnt reservation",
				     __func__, resv_desc_ptr->name);
				if (err_msg)
					*err_msg = xstrdup("Reservation can't be updated with REPLACE or REPLACE_DOWN flags; they should be updated on a NodeCnt reservation");
				error_code = ESLURM_NOT_SUPPORTED;
				goto update_failure;
			}

			/*
			 * If requesting to add the REPLACE or REPLACE_DOWN
			 * flags, and the STATIC or MAINT flags were already
			 * set, then reject the update.
			 */
			if ((resv_ptr->flags & RESERVE_FLAG_STATIC) ||
			    (resv_ptr->flags & RESERVE_FLAG_MAINT)) {
				info("%s: reservation %s can't be updated: REPLACE and REPLACE_DOWN flags cannot be used with STATIC_ALLOC or MAINT flags",
				     __func__, resv_desc_ptr->name);
				if (err_msg)
					*err_msg = xstrdup("REPLACE and REPLACE_DOWN flags cannot be used with STATIC_ALLOC or MAINT flags");
				error_code = ESLURM_NOT_SUPPORTED;
				goto update_failure;
			}


			if (resv_desc_ptr->flags & RESERVE_FLAG_REPLACE)
				resv_ptr->flags |= RESERVE_FLAG_REPLACE;
			else
				resv_ptr->flags |= RESERVE_FLAG_REPLACE_DOWN;
		}
		if ((resv_desc_ptr->flags & RESERVE_FLAG_STATIC) ||
		    (resv_desc_ptr->flags & RESERVE_FLAG_MAINT)) {
			/*
			 * If requesting to add the MAINT or STATIC flag,
			 * and the REPLACE or REPLACE_DOWN flags were already
			 * set, then reject the update.
			 */
			if ((resv_ptr->flags & RESERVE_FLAG_REPLACE) ||
		            (resv_ptr->flags & RESERVE_FLAG_REPLACE_DOWN)) {
		     		info("%s: reservation %s can't be updated: REPLACE and REPLACE_DOWN flags cannot be used with STATIC_ALLOC or MAINT flags",
				     __func__, resv_desc_ptr->name);
				if (err_msg)
					*err_msg = xstrdup("REPLACE and REPLACE_DOWN flags cannot be used with STATIC_ALLOC or MAINT flags");
				error_code = ESLURM_NOT_SUPPORTED;
				goto update_failure;
			}

			if (resv_desc_ptr->flags & RESERVE_FLAG_STATIC)
				resv_ptr->flags |= RESERVE_FLAG_STATIC;
			else
				resv_ptr->flags |= RESERVE_FLAG_MAINT;
		}
		if (resv_desc_ptr->flags & RESERVE_FLAG_PART_NODES) {
			if ((resv_ptr->partition == NULL) &&
			    (resv_desc_ptr->partition == NULL)) {
				info("Reservation %s request can not set "
				     "Part_Nodes flag without partition",
				     resv_desc_ptr->name);
				error_code = ESLURM_INVALID_PARTITION_NAME;
				goto update_failure;
			}
			if (xstrcasecmp(resv_desc_ptr->node_list, "ALL")) {
				info("Reservation %s request can not set Part_Nodes flag without partition and nodes=ALL",
				     resv_desc_ptr->name);
				error_code = ESLURM_INVALID_NODE_NAME;
				goto update_failure;
			}
			if ((resv_ptr->flags & RESERVE_FLAG_REPLACE) ||
			    (resv_ptr->flags & RESERVE_FLAG_REPLACE_DOWN)) {
				info("%s: reservation %s can't be updated with PART_NODES flag; it is incompatible with REPLACE[_DOWN]",
				     __func__, resv_desc_ptr->name);
				error_code = ESLURM_NOT_SUPPORTED;
				goto update_failure;
			}
			resv_ptr->flags |= RESERVE_FLAG_PART_NODES;
			/* Explicitly set the node_list to ALL */
			xfree(resv_desc_ptr->node_list);
			resv_desc_ptr->node_list = xstrdup("ALL");
		}
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_PART_NODES)
			resv_ptr->flags &= (~RESERVE_FLAG_PART_NODES);
		if (resv_desc_ptr->flags & RESERVE_FLAG_TIME_FLOAT) {
			info("Reservation %s request to set TIME_FLOAT flag",
			     resv_desc_ptr->name);
			error_code = ESLURM_INVALID_TIME_VALUE;
			goto update_failure;
		}
		if (resv_desc_ptr->flags & RESERVE_FLAG_PURGE_COMP)
			resv_ptr->flags |= RESERVE_FLAG_PURGE_COMP;
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_PURGE_COMP) {
			resv_ptr->flags &= (~RESERVE_FLAG_PURGE_COMP);
			if (resv_desc_ptr->purge_comp_time == NO_VAL)
				resv_ptr->purge_comp_time = 300;
		}
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_HOLD_JOBS)
			resv_ptr->flags |= RESERVE_FLAG_NO_HOLD_JOBS;
		if ((resv_desc_ptr->flags & RESERVE_FLAG_MAGNETIC) &&
		    !(resv_ptr->flags & RESERVE_FLAG_MAGNETIC)) {
			resv_ptr->flags |= RESERVE_FLAG_MAGNETIC;
			append_magnetic_resv = true;
		}
		if ((resv_desc_ptr->flags & RESERVE_FLAG_NO_MAGNETIC) &&
		    (resv_ptr->flags & RESERVE_FLAG_MAGNETIC)) {
			resv_ptr->flags &= (~RESERVE_FLAG_MAGNETIC);
			remove_magnetic_resv = true;
		}
		if (resv_desc_ptr->flags & RESERVE_FLAG_USER_DEL)
			resv_ptr->flags |= RESERVE_FLAG_USER_DEL;
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_USER_DEL)
			resv_ptr->flags &= (~RESERVE_FLAG_USER_DEL);

		/* handle skipping later */
		if (resv_desc_ptr->flags & RESERVE_FLAG_SKIP) {
			if (!(resv_ptr->flags & RESERVE_REOCCURRING)) {
				error_code = ESLURM_RESERVATION_NO_SKIP;
				goto update_failure;
			}
			skip_it = true;
		}
	}

	if (resv_desc_ptr->max_start_delay != NO_VAL)
		resv_ptr->max_start_delay = resv_desc_ptr->max_start_delay;

	if (resv_desc_ptr->purge_comp_time != NO_VAL)
		resv_ptr->purge_comp_time = resv_desc_ptr->purge_comp_time;

	if (resv_desc_ptr->partition && (resv_desc_ptr->partition[0] == '\0')) {
		/* Clear the partition */
		xfree(resv_desc_ptr->partition);
		xfree(resv_ptr->partition);
		resv_ptr->part_ptr = NULL;
	}
	if (resv_desc_ptr->partition) {
		part_record_t *part_ptr = NULL;
		part_ptr = find_part_record(resv_desc_ptr->partition);
		if (!part_ptr) {
			info("Reservation %s request has invalid partition (%s)",
			     resv_desc_ptr->name, resv_desc_ptr->partition);
			error_code = ESLURM_INVALID_PARTITION_NAME;
			goto update_failure;
		}
		xfree(resv_ptr->partition);
		resv_ptr->partition	= resv_desc_ptr->partition;
		resv_desc_ptr->partition = NULL; /* Nothing left to free */
		resv_ptr->part_ptr	= part_ptr;
	}
	if (resv_desc_ptr->accounts) {
		rc = _update_account_list(resv_ptr, resv_desc_ptr->accounts);
		if (rc) {
			error_code = rc;
			goto update_failure;
		}
	}
	if (resv_desc_ptr->burst_buffer) {
		xfree(resv_ptr->burst_buffer);
		if (resv_desc_ptr->burst_buffer[0] != '\0') {
			resv_ptr->burst_buffer = resv_desc_ptr->burst_buffer;
			resv_desc_ptr->burst_buffer = NULL;
		}
	}
	if (resv_desc_ptr->comment) {
		xfree(resv_ptr->comment);
		if (resv_desc_ptr->comment[0] != '\0') {
			resv_ptr->comment = resv_desc_ptr->comment;
			resv_desc_ptr->comment = NULL;
			info("set it here! %s", resv_ptr->comment);
		}
	}
	if (resv_desc_ptr->licenses && (resv_desc_ptr->licenses[0] == '\0')) {
		if (!resv_desc_ptr->node_cnt ||
		    ((resv_desc_ptr->node_cnt == NO_VAL) &&
		     (resv_ptr->node_cnt == 0))) {
			info("Reservation %s attempt to clear licenses with "
			     "NodeCount=0", resv_desc_ptr->name);
			error_code = ESLURM_INVALID_LICENSES;
			goto update_failure;
		}
		xfree(resv_desc_ptr->licenses);	/* clear licenses */
		xfree(resv_ptr->licenses);
		FREE_NULL_LIST(resv_ptr->license_list);
	}

	if (resv_desc_ptr->licenses) {
		bool valid = true;
		list_t *license_list = NULL;
		license_list = _license_validate2(resv_desc_ptr, &valid);
		if (!valid) {
			info("Reservation %s invalid license update (%s)",
			     resv_desc_ptr->name, resv_desc_ptr->licenses);
			error_code = ESLURM_INVALID_LICENSES;
			goto update_failure;
		}
		xfree(resv_ptr->licenses);
		resv_ptr->licenses	= resv_desc_ptr->licenses;
		resv_desc_ptr->licenses = NULL; /* Nothing left to free */
		FREE_NULL_LIST(resv_ptr->license_list);
		resv_ptr->license_list  = license_list;
	}
	if (resv_desc_ptr->features && (resv_desc_ptr->features[0] == '\0')) {
		xfree(resv_desc_ptr->features);	/* clear features */
		xfree(resv_ptr->features);
	}
	if (resv_desc_ptr->features) {
		/* To support in the future, the reservation resources would
		 * need to be selected again. For now, administrator can
		 * delete this reservation and create a new one. */
		info("Attempt to change features of reservation %s. "
		     "Delete the reservation and create a new one.",
		     resv_desc_ptr->name);
		error_code = ESLURM_NOT_SUPPORTED;
		goto update_failure;
	}

	/* Groups has to be done before users */
	if (resv_desc_ptr->groups) {
		rc = _update_group_uid_list(resv_ptr, resv_desc_ptr->groups);
		if (rc) {
			error_code = rc;
			goto update_failure;
		}
	}

	if (resv_desc_ptr->users) {
		rc = _update_uid_list(resv_ptr, resv_desc_ptr->users);
		if (rc) {
			error_code = rc;
			goto update_failure;
		}
	}

	if (resv_ptr->users && resv_ptr->groups) {
		info("Reservation requested both users and groups, these are mutually exclusive.  You can have one or the other, but not both.");
		error_code = ESLURM_RESERVATION_USER_GROUP;
		goto update_failure;
	}

	if (!resv_ptr->users &&
	    !resv_ptr->accounts &&
	    !resv_ptr->groups) {
		info("Reservation %s request lacks users, accounts or groups",
		     resv_desc_ptr->name);
		error_code = ESLURM_RESERVATION_EMPTY;
		goto update_failure;
	}

	if (resv_desc_ptr->start_time != (time_t) NO_VAL) {
		if (resv_ptr->start_time <= time(NULL)) {
			info("%s: reservation already started", __func__);
			error_code = ESLURM_RSV_ALREADY_STARTED;
			goto update_failure;
		}
		if (resv_desc_ptr->start_time < (now - 60)) {
			info("Reservation %s request has invalid start time",
			     resv_desc_ptr->name);
			error_code = ESLURM_INVALID_TIME_VALUE;
			goto update_failure;
		}
		resv_ptr->start_time_prev = resv_ptr->start_time;
		resv_ptr->start_time = resv_desc_ptr->start_time;
		resv_ptr->start_time_first = resv_desc_ptr->start_time;
		if (resv_ptr->duration != NO_VAL) {
			resv_ptr->end_time = resv_ptr->start_time_first +
				(resv_ptr->duration * 60);
		}
	}
	if (resv_desc_ptr->end_time != (time_t) NO_VAL) {
		if (resv_desc_ptr->end_time < (now - 60)) {
			info("Reservation %s request has invalid end time",
			     resv_desc_ptr->name);
			error_code = ESLURM_INVALID_TIME_VALUE;
			goto update_failure;
		}
		resv_ptr->end_time = resv_desc_ptr->end_time;
		resv_ptr->duration = NO_VAL;
	}

	if (resv_desc_ptr->duration == INFINITE) {
		resv_ptr->duration = YEAR_SECONDS / 60;
		resv_ptr->end_time = resv_ptr->start_time_first + YEAR_SECONDS;
	} else if (resv_desc_ptr->duration != NO_VAL) {
		if (resv_desc_ptr->flags == NO_VAL64)
			resv_ptr->duration = resv_desc_ptr->duration;
		else if (resv_desc_ptr->flags & RESERVE_FLAG_DUR_PLUS)
			resv_ptr->duration += resv_desc_ptr->duration;
		else if (resv_desc_ptr->flags & RESERVE_FLAG_DUR_MINUS) {
			if (resv_ptr->duration >= resv_desc_ptr->duration)
				resv_ptr->duration -= resv_desc_ptr->duration;
			else
				resv_ptr->duration = 0;
		} else
			resv_ptr->duration = resv_desc_ptr->duration;

		resv_ptr->end_time = resv_ptr->start_time_first +
				     (resv_ptr->duration * 60);
		/*
		 * Since duration is a static number we could put the end time
		 * in the past if the reservation already started and we are
		 * removing more time than is left.
		 */
		if (resv_ptr->end_time < now)
			resv_ptr->end_time = now;
	}

	if (resv_ptr->start_time >= resv_ptr->end_time) {
		info("Reservation %s request has invalid times (start > end)",
		     resv_desc_ptr->name);
		error_code = ESLURM_INVALID_TIME_VALUE;
		goto update_failure;
	}
	if (resv_desc_ptr->node_list &&
	    (resv_desc_ptr->node_list[0] == '\0')) {	/* Clear bitmap */
		resv_ptr->flags &= (~RESERVE_FLAG_SPEC_NODES);
		resv_ptr->flags &= (~RESERVE_FLAG_ALL_NODES);
		xfree(resv_desc_ptr->node_list);
		xfree(resv_ptr->node_list);
		FREE_NULL_BITMAP(resv_ptr->node_bitmap);
		FREE_NULL_BITMAP(resv_ptr->core_bitmap);
		free_job_resources(&resv_ptr->core_resrcs);
		resv_ptr->node_bitmap = bit_alloc(node_record_count);
		if (!resv_desc_ptr->node_cnt ||
		    (resv_desc_ptr->node_cnt == NO_VAL)) {
			resv_desc_ptr->node_cnt = resv_ptr->node_cnt;
		}
		resv_ptr->node_cnt = 0;
	}
	if (resv_desc_ptr->node_list) {		/* Change bitmap last */
		if ((resv_ptr->flags & RESERVE_FLAG_REPLACE) ||
		    (resv_ptr->flags & RESERVE_FLAG_REPLACE_DOWN)) {
			info("%s: reservation %s can't be updated with Nodes option; it is incompatible with REPLACE[_DOWN]",
			     __func__, resv_desc_ptr->name);
			if (err_msg)
				*err_msg = xstrdup("Reservation can't be updated with Nodes option; it is incompatible with REPLACE[_DOWN]");
			error_code = ESLURM_NOT_SUPPORTED;
			goto update_failure;
		}
		bitstr_t *node_bitmap;
		resv_ptr->flags |= RESERVE_FLAG_SPEC_NODES;
		if (xstrcasecmp(resv_desc_ptr->node_list, "ALL") == 0) {
			if ((resv_ptr->partition) &&
			    (resv_ptr->flags & RESERVE_FLAG_PART_NODES)) {
				part_record_t *part_ptr = NULL;
				part_ptr = find_part_record(resv_ptr->
							    partition);
				node_bitmap = bit_copy(part_ptr->node_bitmap);
				xfree(resv_ptr->node_list);
				xfree(resv_desc_ptr->node_list);
				resv_ptr->node_list = xstrdup(part_ptr->nodes);
			} else {
				resv_ptr->flags |= RESERVE_FLAG_ALL_NODES;
				node_bitmap = node_conf_get_active_bitmap();
				resv_ptr->flags &= (~RESERVE_FLAG_PART_NODES);
				xfree(resv_ptr->node_list);
				xfree(resv_desc_ptr->node_list);
				resv_ptr->node_list =
					bitmap2node_name(node_bitmap);
			}
		} else {
			resv_ptr->flags &= (~RESERVE_FLAG_PART_NODES);
			resv_ptr->flags &= (~RESERVE_FLAG_ALL_NODES);

			if (!(node_bitmap = _get_update_node_bitmap(
				      resv_ptr, resv_desc_ptr->node_list))) {
				info("Reservation %s request has invalid node name (%s)",
				     resv_desc_ptr->name,
				     resv_desc_ptr->node_list);
				error_code = ESLURM_INVALID_NODE_NAME;
				goto update_failure;
			}

			xfree(resv_desc_ptr->node_list);
			xfree(resv_ptr->node_list);
			resv_ptr->node_list = bitmap2node_name(node_bitmap);
		}
		resv_desc_ptr->node_list = NULL;  /* Nothing left to free */
		FREE_NULL_BITMAP(resv_ptr->node_bitmap);
		FREE_NULL_BITMAP(resv_ptr->core_bitmap);
		free_job_resources(&resv_ptr->core_resrcs);
		resv_ptr->node_bitmap = node_bitmap;
		resv_ptr->node_cnt = bit_set_count(resv_ptr->node_bitmap);
	}
	if (resv_desc_ptr->node_cnt != NO_VAL) {
		resv_ptr->flags &= (~RESERVE_FLAG_PART_NODES);
		resv_ptr->flags &= (~RESERVE_FLAG_ALL_NODES);

		rc = _resize_resv(resv_ptr, resv_desc_ptr->node_cnt);
		if (rc) {
			error_code = rc;
			goto update_failure;
		}
		/*
		 * If the reservation was 0 node count before (ANY_NODES) this
		 * could be NULL, if for some reason someone tried to update the
		 * node count in this situation we will still not have a
		 * node_bitmap.
		 */
		if (resv_ptr->node_bitmap)
			resv_ptr->node_cnt =
				bit_set_count(resv_ptr->node_bitmap);

	}
	slurm_init_resv_desc_msg(&resv_desc);
	resv_desc.start_time  = resv_ptr->start_time;
	resv_desc.end_time    = resv_ptr->end_time;
	resv_desc.flags       = resv_ptr->flags;
	resv_desc.name        = resv_ptr->name;
	if (_resv_overlap(&resv_desc, resv_ptr->node_bitmap, resv_ptr)) {
		info("Reservation %s request overlaps another",
		     resv_desc_ptr->name);
		error_code = ESLURM_RESERVATION_OVERLAP;
		goto update_failure;
	}
	if (_job_overlap(resv_ptr->start_time, resv_ptr->flags,
			 resv_ptr->node_bitmap, resv_desc_ptr->name)) {
		info("Reservation %s request overlaps jobs",
		     resv_desc_ptr->name);
		error_code = ESLURM_NODES_BUSY;
		goto update_failure;
	}

	/* This needs to be after checks for both account and user changes */
	if ((error_code = _set_assoc_list(resv_ptr)) != SLURM_SUCCESS)
		goto update_failure;

	/*
	 * Reject reservation update if we have pending or running jobs using
	 * the reservation, that lose access to the reservation by the update.
	 * This has to happen after _set_assoc_list
	 */
	if ((job_ptr = list_find_first(job_list,
				       _validate_reservation_access_update,
				       resv_ptr))) {
		if (err_msg)
			xstrfmtcat(*err_msg,
				   "Reservation update rejected because of JobId=%u",
				   job_ptr->job_id);
		error_code = ESLURM_RESERVATION_BUSY;
		goto update_failure;
	}

	/*
	 * A reservation without nodes/cores should only be possible if the flag
	 * ANY_NODES is set and it has at least one of licenses or burst buffer.
	 * So test this here after the checks for the involved options.
	 */
	if (!resv_ptr->node_bitmap || (bit_ffs(resv_ptr->node_bitmap)) == -1) {
		if ((resv_ptr->flags & RESERVE_FLAG_ANY_NODES) == 0) {
			info("%s: reservations without nodes are only expected with ANY_NODES flag", __func__);
			error_code = ESLURM_RESERVATION_INVALID;
			goto update_failure;
		} else if ((!resv_ptr->license_list ||
			    list_is_empty(resv_ptr->license_list)) &&
			   !resv_ptr->burst_buffer) {
			info("%s: reservations without nodes and with ANY_NODES flag are expected to be one of Licenses, and/or BurstBuffer", __func__);
			error_code = ESLURM_RESERVATION_INVALID;
			goto update_failure;
		}
	}

	_set_tres_cnt(resv_ptr, resv_backup);

	/* Now check if we are skipping this one */
	if (skip_it) {
		if ((error_code = _delete_resv_internal(resv_ptr)) !=
		    SLURM_SUCCESS)
			goto update_failure;
		if (resv_ptr->start_time > now) {
			resv_ptr->ctld_flags |= RESV_CTLD_EPILOG;
			resv_ptr->ctld_flags |= RESV_CTLD_PROLOG;
		}
		if (_advance_resv_time(resv_ptr) != SLURM_SUCCESS) {
			error_code = ESLURM_RESERVATION_NO_SKIP;
			error("Couldn't skip reservation %s, this should never happen",
			      resv_ptr->name);
			goto update_failure;
		}
	}

	/*
	 * The following two checks need to happen once it is guaranteed the
	 * whole update succeeds, avoiding any path leading to update failure.
	 */
	if (append_magnetic_resv)
		list_append(magnetic_resv_list, resv_ptr);

	if (remove_magnetic_resv)
		(void) list_remove_first(magnetic_resv_list, _find_resv_ptr,
					 resv_ptr);

	_del_resv_rec(resv_backup);
	(void) set_node_maint_mode(true);

	last_resv_update = now;
	schedule_resv_save();
	return error_code;

update_failure:
	/* Restore backup reservation data */
	_restore_resv(resv_ptr, resv_backup);
	_del_resv_rec(resv_backup);
	return error_code;
}

/* Determine if a running or pending job is using a reservation */
static bool _is_resv_used(slurmctld_resv_t *resv_ptr)
{
	if (list_find_first_ro(job_list,
			       _find_running_job_with_resv_ptr,
			       resv_ptr))
		return true;

	return false;
}

/* Clear the reservation pointers for jobs referencing a defunct reservation */
static void _clear_job_resv(slurmctld_resv_t *resv_ptr)
{
	list_for_each(job_list, _foreach_clear_job_resv, resv_ptr);
}

static bool _match_user_assoc(char *assoc_str, list_t *assoc_list, bool deny)
{
	list_itr_t *itr;
	bool found = 0;
	slurmdb_assoc_rec_t *assoc;
	char tmp_char[30];

	if (!assoc_str || !assoc_list || !list_count(assoc_list))
		return false;

	itr = list_iterator_create(assoc_list);
	while ((assoc = list_next(itr))) {
		while (assoc) {
			snprintf(tmp_char, sizeof(tmp_char), ",%s%u,",
				 deny ? "-" : "", assoc->id);
			if (xstrstr(assoc_str, tmp_char)) {
				found = 1;
				goto end_it;
			}
			assoc = assoc->usage->parent_assoc_ptr;
		}
	}
end_it:
	list_iterator_destroy(itr);

	return found;
}

/* Delete an exiting resource reservation */
extern int delete_resv(reservation_name_msg_t *resv_desc_ptr)
{
	list_itr_t *iter;
	slurmctld_resv_t *resv_ptr;
	int rc = SLURM_SUCCESS;

	log_flag(RESERVATION, "%s: Name=%s", __func__, resv_desc_ptr->name);

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(iter))) {
		if (xstrcmp(resv_ptr->name, resv_desc_ptr->name))
			continue;

		if ((rc = _delete_resv_internal(resv_ptr)) !=
		    ESLURM_RESERVATION_BUSY) {
			_clear_job_resv(resv_ptr);
			list_delete_item(iter);
		}
		break;
	}
	list_iterator_destroy(iter);

	if (!resv_ptr) {
		info("Reservation %s not found for deletion",
		     resv_desc_ptr->name);
		return ESLURM_RESERVATION_INVALID;
	}

	last_resv_update = time(NULL);
	schedule_resv_save();
	return rc;
}

extern void reservation_delete_resv_exc_parts(resv_exc_t *resv_exc)
{
	if (!resv_exc)
		return;

	FREE_NULL_BITMAP(resv_exc->core_bitmap);
	free_core_array(&resv_exc->exc_cores);
}

extern void reservation_delete_resv_exc(resv_exc_t *resv_exc)
{
	if (!resv_exc)
		return;

	reservation_delete_resv_exc_parts(resv_exc);
	xfree(resv_exc);
}

/* Return pointer to the named reservation or NULL if not found */
extern slurmctld_resv_t *find_resv_name(char *resv_name)
{
	slurmctld_resv_t *resv_ptr;
	resv_ptr = (slurmctld_resv_t *) list_find_first (resv_list,
			_find_resv_name, resv_name);
	return resv_ptr;
}

/* Dump the reservation records to a buffer */
extern buf_t *show_resv(uid_t uid, uint16_t protocol_version)
{
	list_itr_t *iter;
	slurmctld_resv_t *resv_ptr;
	uint32_t resv_packed;
	int tmp_offset;
	buf_t *buffer;
	time_t now = time(NULL);
	list_t *assoc_list = NULL;
	bool check_permissions = false;
	assoc_mgr_lock_t locks = { .assoc = READ_LOCK };

	DEF_TIMERS;

	START_TIMER;
	_create_resv_lists(false);

	buffer = init_buf(BUF_SIZE);

	/* write header: version and time */
	resv_packed = 0;
	pack32(resv_packed, buffer);
	pack_time(now, buffer);

	/* Create this list once since it will not change during this call. */
	if ((slurm_conf.private_data & PRIVATE_DATA_RESERVATIONS)
	    && !validate_operator(uid)) {
		slurmdb_assoc_rec_t assoc;

		check_permissions = true;

		memset(&assoc, 0, sizeof(slurmdb_assoc_rec_t));
		assoc.uid = uid;

		assoc_list = list_create(NULL);

		assoc_mgr_lock(&locks);
		if (assoc_mgr_get_user_assocs(acct_db_conn, &assoc,
					      accounting_enforce, assoc_list)
		    != SLURM_SUCCESS)
			goto no_assocs;
	}

	/* write individual reservation records */
	iter = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(iter))) {
		if (check_permissions &&
		    !_validate_user_access(resv_ptr, assoc_list, uid))
			continue;

		_pack_resv(resv_ptr, buffer, false, protocol_version);
		resv_packed++;
	}
	list_iterator_destroy(iter);

no_assocs:
	if (check_permissions) {
		FREE_NULL_LIST(assoc_list);
		assoc_mgr_unlock(&locks);
	}

	/* put the real record count in the message body header */
	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack32(resv_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	END_TIMER2(__func__);
	return buffer;
}

/* Save the state of all reservations to file */
extern int dump_all_resv_state(void)
{
	list_itr_t *iter;
	slurmctld_resv_t *resv_ptr;
	int error_code = 0;
	/* Locks: Read node */
	slurmctld_lock_t resv_read_lock = {
		.conf = READ_LOCK,
		.node = READ_LOCK,
	};
	buf_t *buffer = init_buf(BUF_SIZE);
	DEF_TIMERS;

	START_TIMER;
	_create_resv_lists(false);

	/* write header: time */
	packstr(RESV_STATE_VERSION, buffer);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack32(top_suffix, buffer);

	/* write reservation records to buffer */
	lock_slurmctld(resv_read_lock);
	iter = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(iter)))
		_pack_resv(resv_ptr, buffer, true, SLURM_PROTOCOL_VERSION);
	list_iterator_destroy(iter);
	unlock_slurmctld(resv_read_lock);

	error_code = save_buf_to_state("resv_state", buffer, NULL);

	FREE_NULL_BUFFER(buffer);
	END_TIMER2(__func__);
	return error_code;
}

/* Validate one reservation record, return true if good */
static bool _validate_one_reservation(slurmctld_resv_t *resv_ptr)
{
	bool account_not = false, user_not = false;
	slurmctld_resv_t old_resv_ptr;

	if ((resv_ptr->name == NULL) || (resv_ptr->name[0] == '\0')) {
		error("Read reservation without name");
		return false;
	}
	if (_get_core_resrcs(resv_ptr) != SLURM_SUCCESS)
		return false;
	if (resv_ptr->partition) {
		part_record_t *part_ptr = NULL;
		part_ptr = find_part_record(resv_ptr->partition);
		if (!part_ptr) {
			error("Reservation %s has invalid partition (%s)",
			      resv_ptr->name, resv_ptr->partition);
			return false;
		}
		resv_ptr->part_ptr	= part_ptr;
	}
	if (resv_ptr->accounts) {
		int account_cnt = 0, i, rc;
		char **account_list;
		rc = _build_account_list(resv_ptr->accounts,
					 &account_cnt, &account_list,
					 &account_not);
		if (rc) {
			error("Reservation %s has invalid accounts (%s)",
			      resv_ptr->name, resv_ptr->accounts);
			return false;
		}
		for (i=0; i<resv_ptr->account_cnt; i++)
			xfree(resv_ptr->account_list[i]);
		xfree(resv_ptr->account_list);
		resv_ptr->account_cnt  = account_cnt;
		resv_ptr->account_list = account_list;
		if (account_not)
			resv_ptr->ctld_flags |= RESV_CTLD_ACCT_NOT;
		else
			resv_ptr->ctld_flags &= (~RESV_CTLD_ACCT_NOT);
	}
	if (resv_ptr->licenses) {
		bool valid = true;
		FREE_NULL_LIST(resv_ptr->license_list);
		resv_ptr->license_list = license_validate(resv_ptr->licenses,
							  true, true, NULL,
							  &valid);
		if (!valid) {
			error("Reservation %s has invalid licenses (%s)",
			      resv_ptr->name, resv_ptr->licenses);
			return false;
		}
	}
	if (resv_ptr->users) {
		int rc, user_cnt = 0;
		uid_t *user_list = NULL;
		rc = _build_uid_list(resv_ptr->users,
				     &user_cnt, &user_list, &user_not, false);
		if (rc) {
			error("Reservation %s has invalid users (%s)",
			      resv_ptr->name, resv_ptr->users);
			return false;
		}
		xfree(resv_ptr->user_list);
		resv_ptr->user_cnt  = user_cnt;
		resv_ptr->user_list = user_list;
		if (user_not)
			resv_ptr->ctld_flags |= RESV_CTLD_USER_NOT;
		else
			resv_ptr->ctld_flags &= (~RESV_CTLD_USER_NOT);
	}

	if (resv_ptr->groups) {
		int user_cnt = 0;
		uid_t *user_list = get_groups_members(resv_ptr->groups,
						      &user_cnt);

		if (!user_list) {
			error("Reservation %s has invalid groups (%s)",
			      resv_ptr->name, resv_ptr->groups);
			return false;
		}

		xfree(resv_ptr->user_list);
		resv_ptr->user_list = user_list;
		resv_ptr->user_cnt = user_cnt;
		resv_ptr->ctld_flags &= (~RESV_CTLD_USER_NOT);
	}

	if ((resv_ptr->flags & RESERVE_FLAG_PART_NODES) &&
	    resv_ptr->part_ptr && resv_ptr->part_ptr->node_bitmap) {
		memset(&old_resv_ptr, 0, sizeof(slurmctld_resv_t));
		old_resv_ptr.assoc_list = resv_ptr->assoc_list;
		old_resv_ptr.flags = resv_ptr->flags;
		old_resv_ptr.node_list = resv_ptr->node_list;
		resv_ptr->node_list = NULL;
		resv_ptr->node_list = xstrdup(resv_ptr->part_ptr->nodes);
		FREE_NULL_BITMAP(resv_ptr->node_bitmap);
		resv_ptr->node_bitmap = bit_copy(resv_ptr->part_ptr->
						 node_bitmap);
		resv_ptr->node_cnt = bit_set_count(resv_ptr->node_bitmap);
		old_resv_ptr.tres_str = resv_ptr->tres_str;
		resv_ptr->tres_str = NULL;
		_set_tres_cnt(resv_ptr, &old_resv_ptr);
		old_resv_ptr.assoc_list = NULL;
		xfree(old_resv_ptr.tres_str);
		xfree(old_resv_ptr.node_list);
		last_resv_update = time(NULL);
	} else if (resv_ptr->flags & RESERVE_FLAG_ALL_NODES) {
		memset(&old_resv_ptr, 0, sizeof(slurmctld_resv_t));
		old_resv_ptr.assoc_list = resv_ptr->assoc_list;
		old_resv_ptr.flags = resv_ptr->flags;
		old_resv_ptr.node_list = resv_ptr->node_list;
		resv_ptr->node_list = NULL;
		FREE_NULL_BITMAP(resv_ptr->node_bitmap);
		resv_ptr->node_bitmap = node_conf_get_active_bitmap();
		resv_ptr->node_list = bitmap2node_name(resv_ptr->node_bitmap);
		resv_ptr->node_cnt = bit_set_count(resv_ptr->node_bitmap);
		old_resv_ptr.tres_str = resv_ptr->tres_str;
		resv_ptr->tres_str = NULL;
		_set_tres_cnt(resv_ptr, &old_resv_ptr);
		old_resv_ptr.assoc_list = NULL;
		xfree(old_resv_ptr.tres_str);
		xfree(old_resv_ptr.node_list);
		last_resv_update = time(NULL);
	} else if (resv_ptr->node_list) {	/* Change bitmap last */
		/*
		 * Node bitmap must be recreated in any case, i.e. when
		 * they grow because adding new nodes to slurm.conf
		 */
		FREE_NULL_BITMAP(resv_ptr->node_bitmap);
		if (node_name2bitmap(resv_ptr->node_list, false,
				     &resv_ptr->node_bitmap, NULL)) {
			char *new_node_list;
			resv_ptr->node_cnt = bit_set_count(
				resv_ptr->node_bitmap);
			if (!resv_ptr->node_cnt) {
				error("%s: Reservation %s has no nodes left, deleting it",
				      __func__, resv_ptr->name);
				return false;
			}
			memset(&old_resv_ptr, 0, sizeof(slurmctld_resv_t));
			old_resv_ptr.assoc_list = resv_ptr->assoc_list;
			old_resv_ptr.flags = resv_ptr->flags;
			old_resv_ptr.node_list = resv_ptr->node_list;
			resv_ptr->node_list = NULL;
			new_node_list = bitmap2node_name(resv_ptr->node_bitmap);
			info("%s: Reservation %s has invalid previous_nodes:%s remaining_nodes[%d/%u]:%s",
			     __func__, resv_ptr->name, old_resv_ptr.node_list,
			     bit_set_count(resv_ptr->node_bitmap),
			     resv_ptr->node_cnt, new_node_list);
			resv_ptr->node_list = new_node_list;
			new_node_list = NULL;
			old_resv_ptr.tres_str = resv_ptr->tres_str;
			resv_ptr->tres_str = NULL;
			_set_tres_cnt(resv_ptr, &old_resv_ptr);
			old_resv_ptr.assoc_list = NULL;
			xfree(old_resv_ptr.tres_str);
			xfree(old_resv_ptr.node_list);
			last_resv_update = time(NULL);
			schedule_resv_save();
		}
	}

	return true;
}

extern void validate_all_reservations(bool run_now)
{
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	static uint32_t requests = 0;
	bool run;

	if (!run_now) {
		slurm_mutex_lock(&mutex);
		requests++;
		log_flag(RESERVATION, "%s: requests %u",
			 __func__, requests);
		xassert(requests != UINT32_MAX);
		slurm_mutex_unlock(&mutex);
		return;
	}

	slurm_mutex_lock(&mutex);
	run = (requests > 0);
	/* reset requests counter */
	requests = 0;
	slurm_mutex_unlock(&mutex);

	if (run) {
		slurmctld_lock_t lock = {
			.conf = READ_LOCK,
			.job = WRITE_LOCK,
			.node = WRITE_LOCK,
			.part = READ_LOCK,
		};
		lock_slurmctld(lock);
		_validate_all_reservations();
		unlock_slurmctld(lock);
	}
}

static int _validate_job_resv(void *job, void *y)
{
	job_record_t *job_ptr = (job_record_t *)job;
	int rc = SLURM_SUCCESS;

	if (job_ptr->resv_name == NULL)
		return 0;

	if ((job_ptr->resv_ptr == NULL) ||
	    (job_ptr->resv_ptr->magic != RESV_MAGIC))
		rc = validate_job_resv(job_ptr);

	if (!job_ptr->resv_ptr) {
		error("%pJ linked to defunct reservation %s",
		      job_ptr, job_ptr->resv_name);
		job_ptr->resv_id = 0;
		xfree(job_ptr->resv_name);
	}

	if (rc != SLURM_SUCCESS) {
		error("%pJ linked to invalid reservation: %s, holding the job.",
		      job_ptr, job_ptr->resv_name);
		job_ptr->state_reason = WAIT_RESV_INVALID;
		job_state_set_flag(job_ptr, JOB_RESV_DEL_HOLD);
		xstrfmtcat(job_ptr->state_desc,
			   "Reservation %s is invalid",
			   job_ptr->resv_name);
	}
	return 0;
}

/*
 * Validate all reservation records, reset bitmaps, etc.
 * Purge any invalid reservation.
 */
static void _validate_all_reservations(void)
{
	list_itr_t *iter;
	slurmctld_resv_t *resv_ptr;

	/* Make sure we have node write locks. */
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));

	log_flag(RESERVATION, "%s: validating %u reservations and %u jobs",
		 __func__, list_count(resv_list), list_count(job_list));

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(iter))) {
		if (!_validate_one_reservation(resv_ptr)) {
			error("Purging invalid reservation record %s",
			      resv_ptr->name);
			_post_resv_delete(resv_ptr);
			_clear_job_resv(resv_ptr);
			list_delete_item(iter);
		} else {
			_set_assoc_list(resv_ptr);
			top_suffix = MAX(top_suffix, resv_ptr->resv_id);
			_validate_node_choice(resv_ptr);
		}
	}
	list_iterator_destroy(iter);

	/* Validate all job reservation pointers */
	list_for_each(job_list, _validate_job_resv, NULL);
}

/*
 * Replace DOWN, DRAIN or ALLOCATED nodes for reservations with "replace" flag
 */
static void _resv_node_replace(slurmctld_resv_t *resv_ptr)
{
	bitstr_t *preserve_bitmap = NULL;
	bitstr_t *tmp_bitmap = NULL;
	resv_desc_msg_t resv_desc;
	int i, add_nodes, new_nodes, preserve_nodes, busy_nodes_needed;
	bool log_it = true;
	bool replaced = false;
	resv_select_t resv_select = { 0 };

	/* Identify nodes which can be preserved in this reservation */
	preserve_bitmap = bit_copy(resv_ptr->node_bitmap);
	bit_and(preserve_bitmap, avail_node_bitmap);
	if (resv_ptr->flags & RESERVE_FLAG_REPLACE)
		bit_and(preserve_bitmap, idle_node_bitmap);
	preserve_nodes = bit_set_count(preserve_bitmap);

	/*
	 * Try to get replacement nodes, first from idle pool then re-use
	 * busy nodes in the current reservation as needed
	 */
	add_nodes = resv_ptr->node_cnt - preserve_nodes;
	while (add_nodes) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
			char *pres = bitmap2node_name(preserve_bitmap);
			bitstr_t *rem_bitmap = bit_copy(resv_ptr->node_bitmap);
			char *rem = NULL;

			bit_and_not(rem_bitmap, preserve_bitmap);
			rem = bitmap2node_name(rem_bitmap);
			log_flag(RESERVATION, "%s: reservation %s replacing %d/%d nodes unavailable[%d/%"PRId64"]:%s preserving[%d]:%s",
				 __func__, resv_ptr->name, add_nodes,
				 resv_ptr->node_cnt, bit_set_count(rem_bitmap),
				 bit_size(rem_bitmap), rem, preserve_nodes,
				 pres);
			xfree(pres);
			xfree(rem);
			FREE_NULL_BITMAP(rem_bitmap);
		}

		slurm_init_resv_desc_msg(&resv_desc);
		resv_desc.start_time  = resv_ptr->start_time;
		resv_desc.end_time    = resv_ptr->end_time;
		resv_desc.features    = resv_ptr->features;
		resv_desc.flags       = resv_ptr->flags;
		resv_desc.name        = resv_ptr->name;
		resv_desc.tres_str = resv_ptr->tres_str;
		if (!(resv_ptr->ctld_flags & RESV_CTLD_FULL_NODE)) {
			resv_desc.core_cnt = resv_ptr->core_cnt;
		}
		resv_desc.node_cnt = add_nodes;

		resv_desc.job_ptr =
			job_mgr_copy_resv_desc_to_job_record(&resv_desc);
		/* exclude already reserved nodes from new resv request */
		resv_select.node_bitmap =
			bit_copy(resv_ptr->part_ptr->node_bitmap);
		bit_and_not(resv_select.node_bitmap, resv_ptr->node_bitmap);

		i = _select_nodes(&resv_desc, &resv_ptr->part_ptr,
				  &resv_select);
		xfree(resv_desc.node_list);
		xfree(resv_desc.partition);
		if (i == SLURM_SUCCESS) {
			job_record_t *job_ptr = resv_desc.job_ptr;

			replaced = true;
			new_nodes = bit_set_count(resv_select.node_bitmap);
			busy_nodes_needed = resv_ptr->node_cnt - new_nodes
					    - preserve_nodes;
			if (busy_nodes_needed > 0) {
				bit_and_not(resv_ptr->node_bitmap,
					    preserve_bitmap);
				tmp_bitmap = bit_pick_cnt(resv_ptr->node_bitmap,
							  busy_nodes_needed);
				bit_and(resv_ptr->node_bitmap, tmp_bitmap);
				FREE_NULL_BITMAP(tmp_bitmap);
				bit_or(resv_ptr->node_bitmap, preserve_bitmap);
			} else {
				bit_and(resv_ptr->node_bitmap, preserve_bitmap);
			}
			bit_or(resv_ptr->node_bitmap, resv_select.node_bitmap);
			FREE_NULL_BITMAP(resv_select.node_bitmap);
			FREE_NULL_BITMAP(resv_ptr->core_bitmap);
			resv_ptr->core_bitmap = resv_select.core_bitmap;
			resv_select.core_bitmap = NULL;
			free_job_resources(&resv_ptr->core_resrcs);
			resv_ptr->core_resrcs = job_ptr->job_resrcs;
			job_ptr->job_resrcs = NULL;
			xfree(resv_ptr->node_list);
			resv_ptr->node_list = bitmap2node_name(resv_ptr->
							       node_bitmap);
			FREE_NULL_LIST(resv_ptr->gres_list_alloc);
			resv_ptr->gres_list_alloc = job_ptr->gres_list_req;
			gres_job_state_log(resv_ptr->gres_list_alloc, 0);
			job_ptr->gres_list_req = NULL;

			job_record_delete(resv_desc.job_ptr);
			resv_desc.job_ptr = NULL;

			if (log_it ||
			    (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION)) {
				char *kept, *added;
				bitstr_t *new_nodes =
					bit_copy(resv_ptr->node_bitmap);
				bitstr_t *kept_nodes =
					bit_copy(resv_ptr->node_bitmap);

				bit_and_not(new_nodes, preserve_bitmap);
				bit_and(kept_nodes, preserve_bitmap);

				added = bitmap2node_name(new_nodes);
				kept = bitmap2node_name(kept_nodes);

				verbose("%s: modified reservation %s with added[%d/%"PRId64"]:%s kept[%d/%"PRId64"]:%s",
					__func__, resv_ptr->name,
					bit_set_count(new_nodes),
					bit_size(new_nodes), added,
					bit_set_count(kept_nodes),
					bit_size(kept_nodes), kept);

				xfree(kept);
				xfree(added);
				FREE_NULL_BITMAP(new_nodes);
				FREE_NULL_BITMAP(kept_nodes);
			}
			break;
		}
		job_record_delete(resv_desc.job_ptr);
		add_nodes /= 2;	/* Try to get idle nodes as possible */
		if (log_it ||
		    (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION)) {
			verbose("%s: unable to replace all allocated nodes in reservation %s at this time",
				__func__, resv_ptr->name);
			log_it = false;
		}
		_free_resv_select_members(&resv_select);
	}
	FREE_NULL_BITMAP(preserve_bitmap);
	if (replaced) {
		last_resv_update = time(NULL);
		schedule_resv_save();
	}
}

/*
 * Replace DOWN or DRAINED in an advanced reservation, also replaces nodes
 * in use for reservations with the "replace" flag.
 */
static void _validate_node_choice(slurmctld_resv_t *resv_ptr)
{
	int i;
	resv_desc_msg_t resv_desc;
	resv_select_t resv_select = { 0 };

	if ((resv_ptr->node_bitmap == NULL) ||
	    (!(resv_ptr->ctld_flags & RESV_CTLD_FULL_NODE) &&
	     (resv_ptr->node_cnt > 1)) ||
	    (resv_ptr->flags & RESERVE_FLAG_SPEC_NODES) ||
	    (resv_ptr->flags & RESERVE_FLAG_STATIC) ||
	    (resv_ptr->flags & RESERVE_FLAG_MAINT))
		return;

	if ((resv_ptr->flags & RESERVE_FLAG_REPLACE) ||
	    (resv_ptr->flags & RESERVE_FLAG_REPLACE_DOWN)) {
		_resv_node_replace(resv_ptr);
		return;
	}

	i = bit_overlap(resv_ptr->node_bitmap, avail_node_bitmap);
	if (i == resv_ptr->node_cnt) {
		return;
	}

	/* Reservation includes DOWN, DRAINED/DRAINING, FAILING or
	 * NO_RESPOND nodes. Generate new request using _select_nodes()
	 * in attempt to replace these nodes */
	slurm_init_resv_desc_msg(&resv_desc);
	resv_desc.start_time = resv_ptr->start_time;
	resv_desc.end_time   = resv_ptr->end_time;
	resv_desc.features   = resv_ptr->features;
	resv_desc.flags      = resv_ptr->flags;
	resv_desc.name       = resv_ptr->name;
	if (!(resv_ptr->ctld_flags & RESV_CTLD_FULL_NODE)) {
		resv_desc.core_cnt = resv_ptr->core_cnt;
	}
	resv_desc.node_cnt = resv_ptr->node_cnt - i;
	resv_desc.tres_str = resv_ptr->tres_str;

	resv_desc.job_ptr = job_mgr_copy_resv_desc_to_job_record(&resv_desc);
	/* Exclude self reserved nodes only if reservation contains any nodes */
	if (resv_ptr->node_bitmap) {
		resv_select.node_bitmap = bit_copy(avail_node_bitmap);
		bit_and(resv_select.node_bitmap, resv_ptr->part_ptr->node_bitmap);
		bit_and_not(resv_select.node_bitmap, resv_ptr->node_bitmap);
	}

	i = _select_nodes(&resv_desc, &resv_ptr->part_ptr, &resv_select);
	xfree(resv_desc.node_list);
	xfree(resv_desc.partition);
	if (i == SLURM_SUCCESS) {
		job_record_t *job_ptr = resv_desc.job_ptr;
		bit_and(resv_ptr->node_bitmap, avail_node_bitmap);
		bit_or(resv_ptr->node_bitmap, resv_select.node_bitmap);
		FREE_NULL_BITMAP(resv_ptr->core_bitmap);
		resv_ptr->core_bitmap = resv_select.core_bitmap;
		resv_select.core_bitmap = NULL;
		free_job_resources(&resv_ptr->core_resrcs);
		resv_ptr->core_resrcs = job_ptr->job_resrcs;
		job_ptr->job_resrcs = NULL;
		xfree(resv_ptr->node_list);
		resv_ptr->node_list = bitmap2node_name(resv_ptr->node_bitmap);
		FREE_NULL_LIST(resv_ptr->gres_list_alloc);
		resv_ptr->gres_list_alloc = job_ptr->gres_list_req;
		gres_job_state_log(resv_ptr->gres_list_alloc, 0);
		job_ptr->gres_list_req = NULL;
		job_record_delete(resv_desc.job_ptr);
		resv_desc.job_ptr = NULL;
		info("modified reservation %s due to unusable nodes, "
		     "new nodes: %s", resv_ptr->name, resv_ptr->node_list);
	} else if (difftime(resv_ptr->start_time, time(NULL)) < 600) {
		info("reservation %s contains unusable nodes, "
		     "can't reallocate now", resv_ptr->name);
	} else {
		debug("reservation %s contains unusable nodes, "
		      "can't reallocate now", resv_ptr->name);
	}
	job_record_delete(resv_desc.job_ptr);
	_free_resv_select_members(&resv_select);
}

/*
 * Validate if the user has access to this reservation.
 */
static bool _validate_user_access(slurmctld_resv_t *resv_ptr,
				  list_t *user_assoc_list, uid_t uid)
{
	/* Determine if we have access */
	if ((accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS) &&
	    resv_ptr->assoc_list) {
		xassert(user_assoc_list);
		/*
		 * Check to see if the association is
		 * here or the parent association is
		 * listed in the valid associations.
		 */
		if (xstrchr(resv_ptr->assoc_list, '-')) {
			if (_match_user_assoc(resv_ptr->assoc_list,
					      user_assoc_list,
					      true))
				return 0;
		}

		if (xstrstr(resv_ptr->assoc_list, ",1") ||
		    xstrstr(resv_ptr->assoc_list, ",2") ||
		    xstrstr(resv_ptr->assoc_list, ",3") ||
		    xstrstr(resv_ptr->assoc_list, ",4") ||
		    xstrstr(resv_ptr->assoc_list, ",5") ||
		    xstrstr(resv_ptr->assoc_list, ",6") ||
		    xstrstr(resv_ptr->assoc_list, ",7") ||
		    xstrstr(resv_ptr->assoc_list, ",8") ||
		    xstrstr(resv_ptr->assoc_list, ",9") ||
		    xstrstr(resv_ptr->assoc_list, ",0")) {
			if (!_match_user_assoc(resv_ptr->assoc_list,
					       user_assoc_list,
					       false))
				return 0;
		}
	} else {
		for (int i = 0; i < resv_ptr->user_cnt; i++) {
			if (resv_ptr->user_list[i] == uid)
				return 1;
		}
		return 0;
	}

	return 1;
}

/* Open the reservation state save file, or backup if necessary.
 * state_file IN - the name of the state save file used
 * RET the file description to read from or error code
 */
static buf_t *_open_resv_state_file(char **state_file)
{
	buf_t *buf;

	*state_file = xstrdup(slurm_conf.state_save_location);
	xstrcat(*state_file, "/resv_state");
	if (!(buf = create_mmap_buf(*state_file)))
		error("Could not open reservation state file %s: %m",
		      *state_file);
	else
		return buf;

	error("NOTE: Trying backup state save file. Reservations may be lost");
	xstrcat(*state_file, ".old");
	return create_mmap_buf(*state_file);
}

/*
 * Load the reservation state from file, recover on slurmctld restart.
 *	Reset reservation pointers for all jobs.
 *	Execute this after loading the configuration file data.
 * IN recover - 0 = validate current reservations ONLY if already recovered,
 *                  otherwise recover from disk
 *              1+ = recover all reservation state from disk
 * RET SLURM_SUCCESS or error code
 * NOTE: READ lock_slurmctld config before entry
 */
extern int load_all_resv_state(int recover)
{
	char *state_file, *ver_str = NULL;
	time_t now;
	int error_code = 0;
	buf_t *buffer;
	slurmctld_resv_t *resv_ptr = NULL;
	uint16_t protocol_version = NO_VAL16;

	last_resv_update = time(NULL);
	if ((recover == 0) && resv_list) {
		_validate_all_reservations();
		return SLURM_SUCCESS;
	}

	/* Read state file and validate */
	_create_resv_lists(true);

	/* read the file */
	lock_state_files();
	if (!(buffer = _open_resv_state_file(&state_file))) {
		info("No reservation state file (%s) to recover",
		     state_file);
		xfree(state_file);
		unlock_state_files();
		return ENOENT;
	}
	xfree(state_file);
	unlock_state_files();

	safe_unpackstr(&ver_str, buffer);
	debug3("Version string in resv_state header is %s", ver_str);
	if (ver_str && !xstrcmp(ver_str, RESV_STATE_VERSION))
		safe_unpack16(&protocol_version, buffer);

	if (protocol_version == NO_VAL16) {
		if (!ignore_state_errors)
			fatal("Can not recover reservation state, data version incompatible, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
		error("************************************************************");
		error("Can not recover reservation state, data version incompatible");
		error("************************************************************");
		xfree(ver_str);
		FREE_NULL_BUFFER(buffer);
		schedule_resv_save();	/* Schedule save with new format */
		return EFAULT;
	}
	xfree(ver_str);
	safe_unpack_time(&now, buffer);
	safe_unpack32(&top_suffix, buffer);

	while (remaining_buf(buffer) > 0) {
		resv_ptr = _load_reservation_state(buffer, protocol_version);
		if (!resv_ptr)
			break;

		_add_resv_to_lists(resv_ptr);
		info("Recovered state of reservation %s", resv_ptr->name);
	}

	_validate_all_reservations();
	info("Recovered state of %d reservations", list_count(resv_list));
	FREE_NULL_BUFFER(buffer);
	return error_code;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete reservation data checkpoint file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Incomplete reservation data checkpoint file");
	_validate_all_reservations();
	info("Recovered state of %d reservations", list_count(resv_list));
	FREE_NULL_BUFFER(buffer);
	return EFAULT;
}

static int _validate_job_resv_internal(job_record_t *job_ptr,
				      slurmctld_resv_t *resv_ptr)
{
	int rc = _valid_job_access_resv(job_ptr, resv_ptr, true);

	if (rc == SLURM_SUCCESS) {
		if ((resv_ptr->flags & RESERVE_FLAG_PURGE_COMP)
		    && resv_ptr->idle_start_time) {
			log_flag(RESERVATION, "Resetting idle start time to zero on PURGE_COMP reservation %s due to associated %pJ",
				 resv_ptr->name, job_ptr);
		}
		resv_ptr->idle_start_time = 0;
		_validate_node_choice(resv_ptr);
	}

	return rc;
}

/*
 * get_resv_list - find record for named reservation(s)
 * IN name - reservation name(s) in a comma separated char
 * OUT err_part - The first invalid reservation name.
 * RET list of pointers to the reservations or NULL if not found
 * NOTE: Caller must free the returned list
 * NOTE: Caller must free err_part
 */
static int _get_resv_list(job_record_t *job_ptr, char **err_resv)
{
	slurmctld_resv_t *resv_ptr;
	char *token, *last = NULL, *tmp_name;
	int rc = SLURM_SUCCESS;

	xassert(job_ptr);

	if (!xstrchr(job_ptr->resv_name, ','))
		return rc;

	tmp_name = xstrdup(job_ptr->resv_name);
	token = strtok_r(tmp_name, ",", &last);
	if (!token) {
		rc = ESLURM_RESERVATION_INVALID;
		FREE_NULL_LIST(job_ptr->resv_list);
		xfree(*err_resv);
		*err_resv = xstrdup(job_ptr->resv_name);
	}
	while (token) {
		resv_ptr = find_resv_name(token);
		if (resv_ptr) {
			rc = _validate_job_resv_internal(job_ptr, resv_ptr);
			if (rc != SLURM_SUCCESS) {
				FREE_NULL_LIST(job_ptr->resv_list);
				xfree(*err_resv);
				*err_resv = xstrdup(token);
				break;
			}

			if (!job_ptr->resv_list)
				job_ptr->resv_list = list_create(NULL);
			if (!list_find_first(job_ptr->resv_list, _find_resv_ptr,
					     resv_ptr))
				list_append(job_ptr->resv_list, resv_ptr);
		} else {
			FREE_NULL_LIST(job_ptr->resv_list);
			rc = ESLURM_RESERVATION_INVALID;
			if (err_resv) {
				xfree(*err_resv);
				*err_resv = xstrdup(token);
			}
			break;
		}
		token = strtok_r(NULL, ",", &last);
	}
	xfree(tmp_name);

	if (rc == SLURM_SUCCESS)
		list_sort(job_ptr->resv_list, _cmp_resv_id);

	return rc;
}

/*
 * Determine if a job request can use the specified reservations
 *
 * IN/OUT job_ptr - job to validate, set its resv_id
 * RET SLURM_SUCCESS or error code (not found or access denied)
 */
extern int validate_job_resv(job_record_t *job_ptr)
{
	slurmctld_resv_t *resv_ptr = NULL;
	int rc;

	xassert(job_ptr);

	if ((job_ptr->resv_name == NULL) || (job_ptr->resv_name[0] == '\0')) {
		xfree(job_ptr->resv_name);
		job_ptr->resv_id    = 0;
		job_ptr->resv_ptr   = NULL;
		return SLURM_SUCCESS;
	}

	if (!resv_list)
		return ESLURM_RESERVATION_INVALID;

	/* Check to see if we have multiple reservations requested */
	if (xstrchr(job_ptr->resv_name, ',')) {
		char *tmp_str = NULL;

		rc = _get_resv_list(job_ptr, &tmp_str);
		if (tmp_str) {
			error("%pJ requested reservation (%s): %s",
			      job_ptr, tmp_str, slurm_strerror(rc));
			xfree(tmp_str);
		} else /* grab the first on the list to use */
			resv_ptr = list_peek(job_ptr->resv_list);
	} else {
		/* Find the named reservation */
		resv_ptr = find_resv_name(job_ptr->resv_name);
		rc = _validate_job_resv_internal(job_ptr, resv_ptr);
	}

	if (resv_ptr) {
		job_ptr->resv_id  = resv_ptr->resv_id;
		job_ptr->resv_ptr = resv_ptr;
	} else {
		job_ptr->resv_id = 0;
		job_ptr->resv_ptr = NULL;
	}

	return rc;
}

static int  _resize_resv(slurmctld_resv_t *resv_ptr, uint32_t node_cnt)
{
	bitstr_t *tmp2_bitmap = NULL;
	int delta_node_cnt, i, rc;
	resv_desc_msg_t resv_desc;
	resv_select_t resv_select = { 0 };

	delta_node_cnt = resv_ptr->node_cnt - node_cnt;
	if (delta_node_cnt == 0)	/* Already correct node count */
		return SLURM_SUCCESS;

	if (delta_node_cnt > 0) {	/* Must decrease node count */
		if (bit_overlap_any(resv_ptr->node_bitmap, idle_node_bitmap)) {
			/* Start by eliminating idle nodes from reservation */
			resv_select.node_bitmap = bit_copy(resv_ptr->node_bitmap);
			bit_and(resv_select.node_bitmap, idle_node_bitmap);
			i = bit_set_count(resv_select.node_bitmap);
			if (i > delta_node_cnt) {
				tmp2_bitmap = bit_pick_cnt(resv_select.node_bitmap,
							   delta_node_cnt);
				bit_and_not(resv_ptr->node_bitmap, tmp2_bitmap);
				FREE_NULL_BITMAP(resv_select.node_bitmap);
				FREE_NULL_BITMAP(tmp2_bitmap);
				delta_node_cnt = 0;	/* ALL DONE */
			} else if (i) {
				bit_and_not(resv_ptr->node_bitmap,
					idle_node_bitmap);
				resv_ptr->node_cnt = bit_set_count(
						resv_ptr->node_bitmap);
				delta_node_cnt = resv_ptr->node_cnt -
						 node_cnt;
			}
			FREE_NULL_BITMAP(resv_select.node_bitmap);
		}
		if (delta_node_cnt > 0) {
			/* Now eliminate allocated nodes from reservation */
			resv_select.node_bitmap =
				bit_pick_cnt(resv_ptr->node_bitmap, node_cnt);
			FREE_NULL_BITMAP(resv_ptr->node_bitmap);
			resv_ptr->node_bitmap = resv_select.node_bitmap;
		}
		xfree(resv_ptr->node_list);
		resv_ptr->node_list = bitmap2node_name(resv_ptr->node_bitmap);
		resv_ptr->node_cnt = node_cnt;
		return SLURM_SUCCESS;
	}

	/* Ensure if partition exists in reservation otherwise use default */
	if ((rc = _validate_and_set_partition(&resv_ptr->part_ptr,
					      &resv_ptr->partition))) {
		return rc;
	}

	/* Must increase node count. Make this look like new request so
	 * we can use _select_nodes() for selecting the nodes */
	slurm_init_resv_desc_msg(&resv_desc);
	resv_desc.start_time = resv_ptr->start_time;
	resv_desc.end_time   = resv_ptr->end_time;
	resv_desc.features   = resv_ptr->features;
	resv_desc.flags      = resv_ptr->flags;
	resv_desc.node_cnt   = 0 - delta_node_cnt;
	resv_desc.name       = resv_ptr->name;
	resv_desc.tres_str = resv_ptr->tres_str;
	resv_desc.job_ptr = job_mgr_copy_resv_desc_to_job_record(&resv_desc);

	/* Exclude self reserved nodes only if reservation contains any nodes */
	if (resv_ptr->node_bitmap) {
		resv_select.node_bitmap =
			bit_copy(resv_ptr->part_ptr->node_bitmap);
		bit_and_not(resv_select.node_bitmap, resv_ptr->node_bitmap);
	}

	rc = _select_nodes(&resv_desc, &resv_ptr->part_ptr, &resv_select);
	xfree(resv_desc.node_list);
	xfree(resv_desc.partition);
	if (rc == SLURM_SUCCESS) {
		job_record_t *job_ptr = resv_desc.job_ptr;
		/*
		 * If the reservation was 0 node count before (ANY_NODES) this
		 * could be NULL, if for some reason someone tried to update the
		 * node count in this situation we will still not have a
		 * node_bitmap.
		 */
		if (resv_ptr->node_bitmap)
			bit_or(resv_ptr->node_bitmap, resv_select.node_bitmap);
		else
			resv_ptr->node_bitmap = bit_copy(
				resv_select.node_bitmap);
		FREE_NULL_BITMAP(resv_select.node_bitmap);
		FREE_NULL_BITMAP(resv_ptr->core_bitmap);
		resv_ptr->core_bitmap = resv_select.core_bitmap;
		resv_select.core_bitmap = NULL;
		free_job_resources(&resv_ptr->core_resrcs);
		resv_ptr->core_resrcs = job_ptr->job_resrcs;
		job_ptr->job_resrcs = NULL;
		xfree(resv_ptr->node_list);
		resv_ptr->node_list = bitmap2node_name(resv_ptr->node_bitmap);
		resv_ptr->node_cnt = node_cnt;
		FREE_NULL_LIST(resv_ptr->gres_list_alloc);
		resv_ptr->gres_list_alloc = job_ptr->gres_list_req;
		gres_job_state_log(resv_ptr->gres_list_alloc, 0);
		job_ptr->gres_list_req = NULL;
		job_record_delete(resv_desc.job_ptr);
		resv_desc.job_ptr = NULL;
	}
	job_record_delete(resv_desc.job_ptr);

	return rc;
}

static int _feature_has_node_cnt(void *x, void *key)
{
	job_feature_t *feat_ptr = (job_feature_t *) x;

	if (feat_ptr->count > 0)
		return 1;
	return 0;
}

static int _have_mor_feature(void *x, void *key)
{
	job_feature_t *feat_ptr = (job_feature_t *) x;

	if (feat_ptr->op_code == FEATURE_OP_MOR)
		return 1;
	return 0;
}

static int _combine_gres_list_exc(void *object, void *arg)
{
	gres_state_t *gres_state_job_in = object;
	list_t *gres_list_exc = arg;
	gres_job_state_t *gres_js_in = gres_state_job_in->gres_data;
	gres_key_t job_search_key = {
		.config_flags = gres_state_job_in->config_flags,
		.plugin_id = gres_state_job_in->plugin_id,
		.type_id = gres_js_in->type_id,
	};
	gres_state_t *gres_state_job =
		list_find_first(gres_list_exc,
				gres_find_job_by_key_exact_type,
				&job_search_key);

	if (!gres_state_job) {
		gres_state_job = gres_create_state(
			gres_state_job_in,
			GRES_STATE_SRC_STATE_PTR,
			GRES_STATE_TYPE_JOB,
			gres_job_state_dup(gres_js_in));

		list_append(gres_list_exc, gres_state_job);
	} else {
		gres_job_state_t *gres_js = gres_state_job->gres_data;
		gres_js->total_gres += gres_js_in->total_gres;

		/*
		 * At the moment we only care about gres_js->gres_bit_alloc and
		 * gres_js->gres_cnt_node_alloc.
		 */
		if (gres_js_in->gres_bit_alloc) {
			if (!gres_js->gres_bit_alloc)
				gres_js->gres_bit_alloc =
					xcalloc(gres_js->node_cnt,
						sizeof(bitstr_t *));
			for (int i = 0; i < gres_js_in->node_cnt; i++) {
				if (!gres_js_in->gres_bit_alloc[i])
					continue;
				if (!gres_js->gres_bit_alloc[i])
					gres_js->gres_bit_alloc[i] =
						bit_copy(gres_js_in->
							 gres_bit_alloc[i]);
				else
					bit_or(gres_js->gres_bit_alloc[i],
					       gres_js_in->gres_bit_alloc[i]);
			}
		}

		if (gres_js_in->gres_cnt_node_alloc) {
			if (!gres_js->gres_cnt_node_alloc)
				gres_js->gres_cnt_node_alloc =
					xcalloc(gres_js->node_cnt,
						sizeof(uint64_t));
			for (int i = 0; i < gres_js_in->node_cnt; i++) {
				gres_js->gres_cnt_node_alloc[i] +=
					gres_js_in->gres_cnt_node_alloc[i];
			}
		}
	}

	return 1;
}

static void _addto_gres_list_exc(list_t **total_list, list_t *sub_list)
{
	if (!sub_list)
		return;

	if (!*total_list) {
		*total_list = gres_job_state_list_dup(sub_list);
	} else {
		/* Here we have to combine the lists */
		(void) list_for_each(sub_list,
				     _combine_gres_list_exc,
				     *total_list);
	}
}

/*
 * Filter out nodes and cores from reservation based on existing
 * reservations.
 */
static void _filter_resv(resv_desc_msg_t *resv_desc_ptr,
			 slurmctld_resv_t *resv_ptr,
			 resv_select_t *resv_select, bool filter_overlap)
{
	if (!filter_overlap &&
	    ((resv_ptr->flags & RESERVE_FLAG_MAINT) ||
	    (resv_ptr->flags & RESERVE_FLAG_OVERLAP))) {
		log_flag(RESERVATION,
			 "%s: skipping reservation %s filter for reservation %s",
			 __func__, resv_ptr->name, resv_desc_ptr->name);
		return;
	}
	if (resv_ptr->node_bitmap == NULL) {
		log_flag(RESERVATION,
			 "%s: reservation %s has no nodes to filter for reservation %s",
			 __func__, resv_ptr->name, resv_desc_ptr->name);
		return;
	}
	if (!_resv_time_overlap(resv_desc_ptr, resv_ptr)) {
		log_flag(RESERVATION,
			 "%s: reservation %s does not overlap in time to filter for reservation %s",
			  __func__, resv_ptr->name, resv_desc_ptr->name);
		return;
	}
	if (!resv_ptr->core_bitmap &&
	    !(resv_ptr->flags & RESERVE_FLAG_GRES_REQ) &&
	    !(resv_ptr->ctld_flags & RESV_CTLD_FULL_NODE)) {
		error("%s: Reservation %s has no core_bitmap and full_nodes is not set",
		      __func__, resv_ptr->name);
		resv_ptr->ctld_flags |= RESV_CTLD_FULL_NODE;
	}
	if (resv_ptr->ctld_flags & RESV_CTLD_FULL_NODE) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
			char *nodes[2] = {
				bitmap2node_name(resv_ptr->node_bitmap),
				bitmap2node_name(resv_select->node_bitmap)
			};

			log_flag(RESERVATION,
				 "%s: reservation %s filtered nodes:%s from reservation %s nodes:%s",
				 __func__, resv_ptr->name, nodes[0],
				 resv_desc_ptr->name, nodes[1]);

			xfree(nodes[0]);
			xfree(nodes[1]);
		}
		bit_and_not(resv_select->node_bitmap, resv_ptr->node_bitmap);
	}
	if (resv_select->core_bitmap && resv_ptr->core_bitmap) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
			char *cores[2] = {
				bit_fmt_full(resv_ptr->core_bitmap),
				bit_fmt_full(resv_select->core_bitmap)
			};

			log_flag(RESERVATION,
				 "%s: reservation %s filtered cores:%s from reservation %s cores:%s",
				 __func__, resv_ptr->name, cores[0],
				 resv_desc_ptr->name, cores[1]);

			xfree(cores[0]);
			xfree(cores[1]);
		}
		bit_or(resv_select->core_bitmap, resv_ptr->core_bitmap);
	}

	_addto_gres_list_exc(&resv_select->gres_list_exc,
			     resv_ptr->gres_list_alloc);
}

/*
 * Select nodes using given node bitmap and/or core_bitmap
 * Given a reservation create request, select appropriate nodes for use
 * resv_desc_ptr IN - Reservation request, node_list field set on exit
 * part_ptr IN/OUT - Desired partition, if NULL then set to default part
 * resv_bitmap IN/OUT - nodes to use, if points to NULL then used nodes in
 *		specified partition. Set to selected nodes on output.
 * core_bitmap OUT - cores allocated to reservation
 */
static int _select_nodes(resv_desc_msg_t *resv_desc_ptr,
			 part_record_t **part_ptr,
			 resv_select_t *resv_select_ret)
{
	slurmctld_resv_t *resv_ptr;
	resv_select_t resv_select[MAX_BITMAPS] = {{0}};
	int max_bitmap = SELECT_ALL_RSVD;
	time_t now = time(NULL);
	int rc = SLURM_SUCCESS;
	bool have_xand = false;
	list_itr_t *itr;
	job_record_t *job_ptr;

	if ((rc = _validate_and_set_partition(part_ptr,
					      &resv_desc_ptr->partition))) {
		return rc;
	}

	xassert(resv_desc_ptr->job_ptr);
	job_ptr = resv_desc_ptr->job_ptr;

	if (job_ptr->details->min_nodes > job_ptr->details->min_cpus) {
		info("Core count for reservation is less than node count!");
		return ESLURM_INVALID_CORE_CNT;
	}

	xfree(job_ptr->partition);
	job_ptr->partition = xstrdup(resv_desc_ptr->partition);
	job_ptr->part_ptr = *part_ptr;

	if (resv_select_ret->node_bitmap) {
		resv_select[SELECT_ALL_RSVD].node_bitmap =
			resv_select_ret->node_bitmap;
		resv_select_ret->node_bitmap = NULL;
	} else {
		/* Start with all nodes in the partition */
		resv_select[SELECT_ALL_RSVD].node_bitmap =
			bit_copy((*part_ptr)->node_bitmap);
	}

	/* clone online from ALL and then filter down nodes */
	resv_select[SELECT_ONL_RSVD].node_bitmap =
		bit_copy(resv_select[SELECT_ALL_RSVD].node_bitmap);
	bit_and(resv_select[SELECT_ONL_RSVD].node_bitmap, up_node_bitmap);

	/* clone available from ONL and then filter unavailable nodes */
	resv_select[SELECT_AVL_RSVD].node_bitmap =
		bit_copy(resv_select[SELECT_ONL_RSVD].node_bitmap);
	bit_and(resv_select[SELECT_AVL_RSVD].node_bitmap, avail_node_bitmap);

	/* populate other node bitmaps from available (AVL) */
	resv_select[SELECT_NOT_RSVD].node_bitmap =
		bit_copy(resv_select[SELECT_AVL_RSVD].node_bitmap);
	resv_select[SELECT_OVR_RSVD].node_bitmap =
		bit_copy(resv_select[SELECT_AVL_RSVD].node_bitmap);

	/* create core bitmap if cores are requested */
	if (resv_desc_ptr->core_cnt != NO_VAL) {
		node_conf_create_cluster_core_bitmap(
			&resv_select[SELECT_ALL_RSVD].core_bitmap);

		for (int i = 0; i < SELECT_ALL_RSVD; i++)
			resv_select[i].core_bitmap = bit_copy(
				resv_select[SELECT_ALL_RSVD].core_bitmap);
	}

	/*
	 * Filter bitmaps based on selection types.
	 * This needs to be an iterator since _advance_resv_time() may
	 * eventually call _generate_resv_id() which will deadlock the
	 * resv_list lock.
	 */
	itr = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(itr))) {
		if (resv_ptr->end_time <= now)
			(void)_advance_resv_time(resv_ptr);

		_filter_resv(resv_desc_ptr, resv_ptr,
			     &resv_select[SELECT_NOT_RSVD], true);

		_filter_resv(resv_desc_ptr, resv_ptr,
			     &resv_select[SELECT_OVR_RSVD], false);
	}
	list_iterator_destroy(itr);

	if (!(resv_desc_ptr->flags & RESERVE_FLAG_MAINT) &&
	    !(resv_desc_ptr->flags & RESERVE_FLAG_OVERLAP)) {
		/*
		 * Remove reserve red and down nodes unless
		 * MAINT or OVERLAP
		 */
		_free_resv_select_members(&resv_select[SELECT_AVL_RSVD]);
		_free_resv_select_members(&resv_select[SELECT_ONL_RSVD]);
		_free_resv_select_members(&resv_select[SELECT_ALL_RSVD]);
		max_bitmap = SELECT_OVR_RSVD;
	}

	if (!(resv_desc_ptr->flags & RESERVE_FLAG_MAINT) &&
	    (resv_desc_ptr->flags & RESERVE_FLAG_OVERLAP)) {
		/*
		 * Overlap can not select from online/all
		 */
		_free_resv_select_members(&resv_select[SELECT_ONL_RSVD]);
		_free_resv_select_members(&resv_select[SELECT_ALL_RSVD]);
		max_bitmap = SELECT_AVL_RSVD;
	}

	/* Satisfy feature specification */
	if (resv_desc_ptr->features) {
		job_record_t *job_ptr = resv_desc_ptr->job_ptr;
		bool dummy = false;
		int total_node_cnt = 0;

		if (!job_ptr->details->feature_list)
			rc = ESLURM_INVALID_FEATURE;
		else if (list_find_first(job_ptr->details->feature_list,
					 _have_mor_feature, &dummy)) {
			rc = ESLURM_INVALID_FEATURE;
		} else {
			find_feature_nodes(job_ptr->details->feature_list,
					   true);
			if (resv_desc_ptr->node_cnt != NO_VAL) {
				total_node_cnt = resv_desc_ptr->node_cnt;
			}
		}

		if (rc != SLURM_SUCCESS) {
			;
		} else if (list_find_first(job_ptr->details->feature_list,
					   _feature_has_node_cnt, &dummy)) {
			/* take the core_bitmap */
			FREE_NULL_BITMAP(resv_select_ret->core_bitmap);
			resv_select_ret->core_bitmap =
				resv_select[max_bitmap].core_bitmap;
			resv_select[max_bitmap].core_bitmap = NULL;

			/* Accumulate resources by feature type/count */
			have_xand = true;
			_pick_nodes_by_feature_node_cnt(
				resv_select[max_bitmap].node_bitmap,
				resv_desc_ptr,
				resv_select_ret,
				total_node_cnt,
				job_ptr->details->feature_list);
		} else {
			/*
			 * Simple AND/OR node filtering.
			 * First try to use nodes with the feature active.
			 * If that fails, use nodes with the feature available.
			 */
			bitstr_t *tmp_bitmap;
			tmp_bitmap = bit_copy(resv_select[max_bitmap].
					      node_bitmap);
			rc = valid_feature_counts(job_ptr, true, tmp_bitmap,
						  &dummy);
			if ((rc == SLURM_SUCCESS) &&
			    (bit_set_count(tmp_bitmap) < total_node_cnt)) {
				/* reset tmp_bitmap and try with available */
				bit_clear_all(tmp_bitmap);
				bit_or(tmp_bitmap,
				       resv_select[max_bitmap].node_bitmap);
				rc = valid_feature_counts(job_ptr, false,
							  tmp_bitmap, &dummy);
			}

			if ((rc == SLURM_SUCCESS) &&
			    bit_set_count(tmp_bitmap) < total_node_cnt)
				rc = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;

			/* filter nodes that won't work from all bitmaps */
			for (size_t i = 0; (i < MAX_BITMAPS) &&
				     resv_select[i].node_bitmap;
			     i++)
				bit_and(resv_select[i].node_bitmap, tmp_bitmap);
			FREE_NULL_BITMAP(tmp_bitmap);
		}
	}

	if (!have_xand && (rc == SLURM_SUCCESS)) {
		rc = _pick_nodes_ordered(resv_desc_ptr,
					 resv_select, MAX_BITMAPS,
					 resv_select_ret,
					 select_node_bitmap_tags);
	}

	/* release all the resv_select */
	for (size_t i = 0; (i < MAX_BITMAPS); i++)
		_free_resv_select_members(&resv_select[i]);

	/* No idle nodes found */
	if ((resv_select_ret->node_bitmap == NULL) && (rc == SLURM_SUCCESS))
		rc = ESLURM_NODES_BUSY;

	if (!resv_desc_ptr->node_list)
		resv_desc_ptr->node_list =
			bitmap2node_name(resv_select_ret->node_bitmap);

	return rc;
}

static void _pick_nodes_by_feature_node_cnt(bitstr_t *avail_bitmap,
					    resv_desc_msg_t *resv_desc_ptr,
					    resv_select_t *resv_select_ret,
					    int total_node_cnt,
					    list_t *feature_list)
{
	bitstr_t *tmp_bitmap = NULL;
	bitstr_t *feature_bitmap;
	uint32_t save_core_cnt;
	uint32_t save_node_cnt;
	uint32_t save_min_cpus, save_min_nodes, save_max_nodes;
	job_feature_t *feat_ptr;
	list_itr_t *feat_iter;
	int paren = 0;
	bool test_active = true;
	job_record_t *job_ptr = resv_desc_ptr->job_ptr;
	job_details_t *detail_ptr = job_ptr->details;
	resv_select_t resv_select = { 0 };

	save_min_cpus = detail_ptr->min_cpus;
	save_min_nodes = detail_ptr->min_nodes;
	save_max_nodes = detail_ptr->max_nodes;

	save_core_cnt = resv_desc_ptr->core_cnt;
	resv_desc_ptr->core_cnt = NO_VAL;
	save_node_cnt = resv_desc_ptr->node_cnt;
	resv_desc_ptr->node_cnt = NO_VAL;

TRY_AVAIL:
	/*
	 * In the first pass, we try to satisfy the resource requirements using
	 * currently active features. If that fails, use available features
	 * and require a reboot to satisfy the request
	 */
	feat_iter = list_iterator_create(feature_list);
	while ((feat_ptr = list_next(feat_iter))) {
		feature_bitmap = test_active ?
			feat_ptr->node_bitmap_active :
			feat_ptr->node_bitmap_avail;
		if (feat_ptr->paren > paren) {	/* Start parenthesis */
			paren = feat_ptr->paren;
			tmp_bitmap = feature_bitmap;
			continue;
		}
		if ((feat_ptr->paren == 1) ||	 /* Continue parenthesis */
		    (feat_ptr->paren < paren)) { /* End of parenthesis */
			paren = feat_ptr->paren;
			bit_and(feature_bitmap, tmp_bitmap);
			tmp_bitmap = feature_bitmap;
			if (feat_ptr->paren == 1)
				continue;
		}

		detail_ptr->orig_min_cpus =
			detail_ptr->num_tasks =
			detail_ptr->min_cpus =
			detail_ptr->max_nodes =
			detail_ptr->min_nodes =
			resv_desc_ptr->node_cnt =
			feat_ptr->count ? feat_ptr->count : 1;
		resv_select.node_bitmap = bit_copy(avail_bitmap);
		bit_and(resv_select.node_bitmap, feature_bitmap);
		resv_select.core_bitmap = resv_select_ret->core_bitmap;
		_pick_nodes(resv_desc_ptr, &resv_select, resv_select_ret);
		FREE_NULL_BITMAP(resv_select.node_bitmap);
		if (!resv_select_ret->node_bitmap)
			break;
	}
	list_iterator_destroy(feat_iter);
	if (!resv_select_ret->node_bitmap && test_active) {
		/* Test failed for active features, test available features */
		test_active = false;
		goto TRY_AVAIL;
	}

	/*
	 * We have picked all the featured nodes, if we requested more nodes we
	 * will now pick those non-featured nodes.
	 */
	if (resv_select_ret->node_bitmap &&
	    (bit_set_count(resv_select_ret->node_bitmap) < total_node_cnt)) {
		detail_ptr->orig_min_cpus =
			detail_ptr->num_tasks =
			detail_ptr->min_cpus =
			detail_ptr->max_nodes =
			detail_ptr->min_nodes =
			resv_desc_ptr->node_cnt =
			total_node_cnt -
			bit_set_count(resv_select_ret->node_bitmap);
		resv_select.node_bitmap = bit_copy(avail_bitmap);
		resv_select.core_bitmap = resv_select_ret->core_bitmap;
		_pick_nodes(resv_desc_ptr, &resv_select, resv_select_ret);
		FREE_NULL_BITMAP(resv_select.node_bitmap);
	}

	detail_ptr->orig_min_cpus =
		detail_ptr->num_tasks =
		detail_ptr->min_cpus =
		save_min_cpus;
	detail_ptr->min_nodes = save_min_nodes;
	detail_ptr->max_nodes = save_max_nodes;

	resv_desc_ptr->core_cnt = save_core_cnt;
	resv_desc_ptr->node_cnt = save_node_cnt;
}

/*
 * Build core_resrcs based upon node_bitmap and core_bitmap as needed.
 * This translates a global core_bitmap (including all nodes) to a
 * core_bitmap for only those nodes in the reservation. This is needed to
 * handle nodes being added or removed from the system or their core count
 * changing.
 */
static void _validate_core_resrcs(resv_desc_msg_t *resv_desc_ptr,
				  bitstr_t *node_bitmap,
				  bitstr_t *core_bitmap)
{
	job_record_t *job_ptr = resv_desc_ptr->job_ptr;
	node_record_t *node_ptr;
	int node_inx, rc;
	int core_offset_local, core_offset_global, core_end;

	xassert(job_ptr);

	/*
	 * In most cases if we have a core_bitmap the core_resrcs will already
	 * be correct. In that case just continue.
	 */
	if (!core_bitmap ||
	    !job_ptr->job_resrcs ||
	    !job_ptr->job_resrcs->core_bitmap ||
	    (bit_set_count(job_ptr->job_resrcs->core_bitmap) ==
	     bit_set_count(core_bitmap)) ||
	    !bit_set_count(node_bitmap))
		return;

	free_job_resources(&job_ptr->job_resrcs);

	job_ptr->job_resrcs = create_job_resources();
	job_ptr->job_resrcs->nodes = bitmap2node_name(node_bitmap);
	job_ptr->job_resrcs->node_bitmap = bit_copy(node_bitmap);
	job_ptr->job_resrcs->nhosts = bit_set_count(node_bitmap);
	rc = build_job_resources(job_ptr->job_resrcs);
	if (rc != SLURM_SUCCESS) {
		free_job_resources(&job_ptr->job_resrcs);
		return;
	}

	job_ptr->job_resrcs->cpus =
		xcalloc(job_ptr->job_resrcs->nhosts, sizeof(uint16_t));

	core_offset_local = -1;
	node_inx = -1;
	for (int i = 0; (node_ptr = next_node_bitmap(node_bitmap, &i)); i++) {
		node_inx++;
		core_offset_global = cr_get_coremap_offset(i);
		core_end = cr_get_coremap_offset(i + 1);
		for (int c = core_offset_global; c < core_end; c++) {
			core_offset_local++;
			if (!bit_test(core_bitmap, c))
				continue;
			if (job_ptr->job_resrcs->core_bitmap)
				bit_set(job_ptr->job_resrcs->core_bitmap,
					core_offset_local);
			job_ptr->job_resrcs->cpus[node_inx] +=
				node_ptr->threads;
			job_ptr->job_resrcs->ncpus += node_ptr->threads;
		}
	}
}

/*
 * Pick nodes based on ordered list of bitmaps
 * IN/OUT resv_desc_ptr - Reservation requesting nodes.
 * 	node_list will be updated every run.
 * IN/OUT resv_select - array of size MAX_BITMAPS, last pointer must be NULL.
 *        IN            .node_bitmap - Ordered list of nodes that could be used
 *                                     for the reservation. Will attempt to use
 *                                     nodes from low ordered bitmaps first.
 *        IN/OUT        .core_bitmap - Ordered list of cores that could be used
 *                                     for the reservation. Will attempt to use
 *                                     cores from low ordered bitmaps
 *                                     first. Cores must match nodes in same
 *                                     node avail_bitmap. Cores will be updated
 *                                     as chosen.
 * OUT resv_select_ret - on success set new bitmaps of allocation.
 * IN bitmap_tags - NULL, or array of cstrings giving a tag for each array index
 * 	in the bitmaps
 * RET SLURM_SUCCESS or error
 */
static int _pick_nodes_ordered(resv_desc_msg_t *resv_desc_ptr,
			       resv_select_t *resv_select,
			       int resv_select_cnt,
			       resv_select_t *resv_select_ret,
			       const char **bitmap_tags)
{
	bitstr_t *selected_bitmap =
		bit_alloc(bit_size(resv_select[0].node_bitmap));
	bitstr_t *selected_core_bitmap = NULL;
	size_t remain_nodes = (resv_desc_ptr->node_cnt == NO_VAL) ?
		0 : resv_desc_ptr->node_cnt;
	size_t remain_cores = (resv_desc_ptr->core_cnt == NO_VAL) ?
		0 : resv_desc_ptr->core_cnt;

	if (resv_select[0].core_bitmap)
		selected_core_bitmap =
			bit_alloc(bit_size(resv_select[0].core_bitmap));

	if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
		char *cores = NULL, *nodes = NULL, *pos = NULL;
		size_t max_bitmap = 0;

		for (size_t b = 0; (b < resv_select_cnt) &&
			     resv_select[b].node_bitmap;
		     b++) {
			char *tmp = bitmap2node_name(
				resv_select[b].node_bitmap);
			xstrfmtcatat(nodes, &pos, "%s%s[%zu]=%s",
				     (b == 0 ? "" : ","),
				     (bitmap_tags ? bitmap_tags[b] : ""),
				     b,
				     ((!tmp || !tmp[0]) ? "(NONE)" : tmp));
			xfree(tmp);

			max_bitmap = MAX(max_bitmap, (b + 1));
		}
		pos = NULL;

		for (size_t b = 0; (b < resv_select_cnt) &&
			     resv_select[b].core_bitmap; b++) {
			char *tmp = bit_fmt_full(resv_select[b].core_bitmap);
			xstrfmtcatat(cores, &pos, "%s%s[%zu]=%s",
				     (b == 0 ? "" : ","),
				     (bitmap_tags ? bitmap_tags[b] : ""),
				     b,
				     ((!tmp || !tmp[0]) ? "(NONE)" : tmp));
			xfree(tmp);

			max_bitmap = MAX(max_bitmap, (b + 1));
		}
		pos = NULL;

		log_flag(RESERVATION, "%s: reservation %s picking from %zu bitmaps avail_nodes_bitmaps[%u]:%s used_cores_bitmaps[%u]:%s",
			 __func__, resv_desc_ptr->name, max_bitmap,
			 resv_desc_ptr->node_cnt,
			 nodes, resv_desc_ptr->core_cnt,
			 (cores ? cores : "(NONE)"));

		xfree(cores);
		xfree(nodes);
	}

	/* Free node_list here, it could be filled in by the select plugin. */
	xfree(resv_desc_ptr->node_list);
	if (resv_desc_ptr->flags & RESERVE_FLAG_GRES_REQ) {
		remain_cores = 1;
	}

	for (size_t b = 0; (remain_nodes || remain_cores) &&
		     (b < resv_select_cnt) && resv_select[b].node_bitmap; b++) {
		bitstr_t *tmp_bitmap;
		size_t nodes_picked, cores_picked = 0;

		/* Avoid picking already picked nodes */
		bit_and_not(resv_select[b].node_bitmap, selected_bitmap);
		if (selected_core_bitmap)
			bit_and_not(resv_select[b].core_bitmap,
				    selected_core_bitmap);

		if (!bit_set_count(resv_select[b].node_bitmap)) {
			log_flag(RESERVATION, "%s: reservation %s skipping empty bitmap:%s[%zu]",
				 __func__, resv_desc_ptr->name,
				 (bitmap_tags ? bitmap_tags[b] : ""),
				 b);
			continue;
		}

		tmp_bitmap = _pick_node_cnt(
			resv_desc_ptr, &resv_select[b], remain_nodes);
		if (tmp_bitmap == NULL) {	/* allocation failure */
			log_flag(RESERVATION, "%s: reservation %s of 0/%zu nodes with bitmap:%s[%zu]",
				 __func__, resv_desc_ptr->name,
				 remain_nodes,
				 (bitmap_tags ? bitmap_tags[b] : ""),
				 b);
			continue;
		}

		/* avoid counting already reserved nodes */
		bit_and_not(tmp_bitmap, selected_bitmap);

		/* grab counts of picked resources */
		nodes_picked = bit_set_count(tmp_bitmap);
		if (resv_select[b].core_bitmap)
			cores_picked = bit_set_count(resv_select[b].core_bitmap);

		if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
			char *nodes = bitmap2node_name(tmp_bitmap);
			char *cores = NULL;

			if (resv_select[b].core_bitmap)
				cores = bit_fmt_full(resv_select[b].core_bitmap);

			log_flag(RESERVATION, "%s: reservation %s picked from bitmap:%s[%zu] nodes[%zu/%zu]:%s cores[%zu]:%s",
				 __func__, resv_desc_ptr->name,
				 (bitmap_tags ? bitmap_tags[b] : ""), b,
				 remain_nodes, nodes_picked, nodes,
				 cores_picked, cores);

			xfree(nodes);
			xfree(cores);
		}

		if (nodes_picked <= remain_nodes)
			remain_nodes -= nodes_picked;
		else
			remain_nodes = 0;

		if (resv_select[b].core_bitmap) {
			if (cores_picked <= remain_cores)
				remain_cores -= cores_picked;
			else
				remain_cores = 0;

			if (!selected_core_bitmap) {
				/*
				 * select plugin made a core bitmap, use
				 * it for selected cores instead
				 */
				selected_core_bitmap =
					resv_select[b].core_bitmap;
				resv_select[b].core_bitmap = NULL;
			} else
				bit_or(selected_core_bitmap,
				       resv_select[b].core_bitmap);
		}
		bit_or(selected_bitmap, tmp_bitmap);
		bit_and_not(resv_select[b].node_bitmap, tmp_bitmap);
		FREE_NULL_BITMAP(tmp_bitmap);

		if (!remain_nodes) {
			log_flag(RESERVATION, "%s: reservation %s selected sufficient nodes by bitmap:%s[%zu]",
				 __func__, resv_desc_ptr->name,
				 (bitmap_tags ? bitmap_tags[b] : ""),
				 b);
		} else if (selected_core_bitmap && !remain_cores) {
			log_flag(RESERVATION, "%s: reservation %s selected sufficient cores by bitmap:%s[%zu]",
				 __func__, resv_desc_ptr->name,
				 (bitmap_tags ? bitmap_tags[b] : ""),
				 b);
		} else {
			log_flag(RESERVATION, "%s: reservation %s requires nodes:%zu cores:%zu after bitmap:%s[%zu]",
				 __func__, resv_desc_ptr->name,
				 remain_nodes, remain_cores,
				 (bitmap_tags ? bitmap_tags[b] : ""),
				 b);
		}
	}

	/* If nothing selected, return a NULL pointer instead */
	if (!selected_bitmap || !bit_set_count(selected_bitmap)) {
		log_flag(RESERVATION, "%s: reservation %s unable to pick any nodes",
			 __func__, resv_desc_ptr->name);
		FREE_NULL_BITMAP(selected_bitmap);
		FREE_NULL_BITMAP(selected_core_bitmap);
		return ESLURM_NODES_BUSY;
	} else {
		_validate_core_resrcs(resv_desc_ptr, selected_bitmap,
				      selected_core_bitmap);

		if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
			char *nodes = NULL;
			int node_cnt = 0;
			char *cores = NULL;
			int core_cnt = 0;

			if (selected_bitmap) {
				nodes = bitmap2node_name(selected_bitmap);
				node_cnt = bit_set_count(selected_bitmap);
			}
			if (selected_core_bitmap) {
				cores = bit_fmt_full(selected_core_bitmap);
				core_cnt = bit_set_count(selected_core_bitmap);
			}
			log_flag(RESERVATION, "%s: reservation %s picked nodes[%u]:%s cores[%u]:%s",
				 __func__, resv_desc_ptr->name, node_cnt, nodes,
				 core_cnt, cores);
			xfree(nodes);
			xfree(cores);
		}

		if (resv_select_ret->node_bitmap) {
			bit_or(resv_select_ret->node_bitmap, selected_bitmap);
			FREE_NULL_BITMAP(selected_bitmap);
		} else {
			resv_select_ret->node_bitmap = selected_bitmap;
		}

		resv_select_ret->core_bitmap = selected_core_bitmap;
		return SLURM_SUCCESS;
	}
}

/*
 * Select nodes using given a single node bitmap and/or core_bitmap
 */
static void _pick_nodes(resv_desc_msg_t *resv_desc_ptr,
			resv_select_t *resv_select,
			resv_select_t *resv_select_ret)
{
	xassert(resv_select);

	if (resv_select_ret->node_bitmap)
		bit_and_not(resv_select->node_bitmap,
			    resv_select_ret->node_bitmap);

	if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
		char *nodes = NULL;
		int node_cnt = 0;
		char *cores = NULL;
		int core_cnt = 0;

		if (resv_select->node_bitmap) {
			nodes = bitmap2node_name(resv_select->node_bitmap);
			node_cnt = bit_set_count(resv_select->node_bitmap);
		}
		if (resv_select->core_bitmap) {
			cores = bit_fmt_full(resv_select->core_bitmap);
			core_cnt = bit_set_count(resv_select->core_bitmap);
		}
		log_flag(RESERVATION, "%s: reservation %s picking nodes[%u]:%s cores[%u]:%s",
			 __func__, resv_desc_ptr->name, node_cnt, nodes,
			 core_cnt, cores);
		xfree(nodes);
		xfree(cores);
	}

	if (_pick_nodes_ordered(resv_desc_ptr,
				resv_select, 1,
				resv_select_ret,
				(select_node_bitmap_tags + SELECT_ALL_RSVD))) {
		/* If picking nodes failed clear ret_node_bitmap */
		_free_resv_select_members(resv_select_ret);
	}
}

static void _check_job_compatibility(job_record_t *job_ptr,
				     resv_select_t *resv_select)
{
	uint32_t total_nodes;
	bitstr_t *full_node_bitmap;
	int i_core, i_node, res_inx;
	int start = 0;
	int rep_count = 0;
	job_resources_t *job_res = job_ptr->job_resrcs;

	if (!job_res->core_bitmap)
		return;

	total_nodes = bit_set_count(job_res->node_bitmap);

	if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
		char str[200];
		bit_fmt(str, sizeof(str), job_res->core_bitmap);
		log_flag(RESERVATION, "%s: Checking %d nodes (of %"PRIu64") for %pJ, core_bitmap:%s core_bitmap_size:%"PRIu64,
		     __func__, total_nodes, bit_size(job_res->node_bitmap),
		     job_ptr, str, bit_size(job_res->core_bitmap));
	}

	full_node_bitmap = bit_copy(job_res->node_bitmap);
	node_conf_create_cluster_core_bitmap(&resv_select->core_bitmap);

	i_node = 0;
	res_inx = 0;
	while (i_node < total_nodes) {
		int cores_in_a_node = (job_res->sockets_per_node[res_inx] *
				       job_res->cores_per_socket[res_inx]);
		int repeat_node_conf = job_res->sock_core_rep_count[rep_count++];
		int node_bitmap_inx;

		log_flag(RESERVATION, "%s: Working with %d cores per node. Same node conf repeated %d times (start core offset %d)",
		     __func__, cores_in_a_node, repeat_node_conf, start);

		i_node += repeat_node_conf;
		res_inx++;

		while (repeat_node_conf--) {
			int allocated;
			int global_core_start;

			node_bitmap_inx = bit_ffs(full_node_bitmap);
			if (node_bitmap_inx < 0)
				break;	/* No more nodes */
			global_core_start =
				cr_get_coremap_offset(node_bitmap_inx);
			allocated = 0;

			for (i_core = 0; i_core < cores_in_a_node; i_core++) {
				log_flag(RESERVATION, "%s: %pJ i_core: %d, start: %d, allocated: %d",
					 __func__, job_ptr, i_core, start,
					 allocated);

				if (bit_test(job_ptr->job_resrcs->core_bitmap,
					     i_core + start)) {
					allocated++;
					bit_set(resv_select->core_bitmap,
						global_core_start + i_core);
				}
			}
			log_flag(RESERVATION, "%s: Checking node %d, allocated: %d, cores_in_a_node: %d",
				 __func__, node_bitmap_inx, allocated,
				 cores_in_a_node);

			if (allocated == cores_in_a_node) {
				/* We can exclude this node */
				log_flag(RESERVATION, "%s: %pJ excluding node %d",
					 __func__, job_ptr, node_bitmap_inx);
				bit_clear(resv_select->node_bitmap,
					  node_bitmap_inx);
			}
			start += cores_in_a_node;
			bit_clear(full_node_bitmap, node_bitmap_inx);
		}
	}
	FREE_NULL_BITMAP(full_node_bitmap);
}

static bitstr_t *_pick_node_cnt(resv_desc_msg_t *resv_desc_ptr,
				resv_select_t *resv_select,
				uint32_t node_cnt)
{
	list_itr_t *job_iterator;
	job_record_t *job_ptr;
	bitstr_t *orig_bitmap = NULL, *save_bitmap = NULL;
	bitstr_t *ret_bitmap = NULL, *tmp_bitmap = NULL;
	int total_node_cnt;
	resv_select_t orig_resv_select = { 0 };

	if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
		orig_resv_select.node_bitmap =
			bit_copy(resv_select->node_bitmap);
		if (resv_select->core_bitmap)
			orig_resv_select.core_bitmap =
				bit_copy(resv_select->core_bitmap);
	}

	total_node_cnt = bit_set_count(resv_select->node_bitmap);
	if (total_node_cnt < node_cnt) {
		verbose("%s: reservation %s requests %d of %d nodes. Reducing requested node count.",
			__func__, resv_desc_ptr->name, node_cnt,
			total_node_cnt);
		node_cnt = total_node_cnt;
	}

	if ((total_node_cnt == node_cnt) &&
		   (resv_desc_ptr->flags & RESERVE_FLAG_IGN_JOBS)) {
		log_flag(RESERVATION, "%s: reservation %s requests all %d nodes",
			__func__, resv_desc_ptr->name, total_node_cnt);
		ret_bitmap = _resv_select(resv_desc_ptr, resv_select);
		goto fini;
	} else if ((node_cnt == 0) &&
		   (resv_desc_ptr->core_cnt == NO_VAL) &&
		   (resv_desc_ptr->flags & RESERVE_FLAG_ANY_NODES)) {
		log_flag(RESERVATION, "%s: reservation %s requests any of all %d nodes",
			__func__, resv_desc_ptr->name, total_node_cnt);
		ret_bitmap = bit_alloc(bit_size(resv_select->node_bitmap));
		goto fini;
	}

	orig_bitmap = bit_copy(resv_select->node_bitmap);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr))
			continue;
		if (job_ptr->end_time < resv_desc_ptr->start_time)
			continue;

		if (resv_desc_ptr->core_cnt == NO_VAL) {
			bit_and_not(resv_select->node_bitmap, job_ptr->node_bitmap);
		} else if (!(resv_desc_ptr->flags & RESERVE_FLAG_IGN_JOBS)) {
			/*
			 * _check_job_compatibility will remove nodes and cores
			 * from the available bitmaps if those resources are
			 * being used by jobs. Don't do this if the IGNORE_JOBS
			 * flag is set.
			 */
			_check_job_compatibility(job_ptr, resv_select);
		}
	}
	list_iterator_destroy(job_iterator);

	total_node_cnt = bit_set_count(resv_select->node_bitmap);
	if (total_node_cnt >= node_cnt) {
		/*
		 * NOTE: _resv_select() does NOT preserve
		 * resv_select->node_bitmap,
		 * so we do that here and other calls to that function.
		 */
		save_bitmap = bit_copy(resv_select->node_bitmap);
		ret_bitmap = _resv_select(resv_desc_ptr, resv_select);
		if (ret_bitmap)
			goto fini;
		bit_or(resv_select->node_bitmap, save_bitmap);
		FREE_NULL_BITMAP(save_bitmap);
	}

	/* Next: Try to reserve nodes that will be allocated to a limited
	 * number of running jobs. We could sort the jobs by priority, QOS,
	 * size or other criterion if desired. Right now we just go down
	 * the unsorted job list. */
	if (resv_desc_ptr->flags & RESERVE_FLAG_IGN_JOBS) {
		job_iterator = list_iterator_create(job_list);
		while ((job_ptr = list_next(job_iterator))) {
			if (!IS_JOB_RUNNING(job_ptr) &&
			    !IS_JOB_SUSPENDED(job_ptr))
				continue;
			if (job_ptr->end_time < resv_desc_ptr->start_time)
				continue;
			tmp_bitmap = bit_copy(orig_bitmap);
			bit_and(tmp_bitmap, job_ptr->node_bitmap);
			if (bit_set_count(tmp_bitmap) > 0)
				bit_or(resv_select->node_bitmap, tmp_bitmap);
			total_node_cnt = bit_set_count(
				resv_select->node_bitmap);
			if (total_node_cnt >= node_cnt) {
				save_bitmap = bit_copy(
					resv_select->node_bitmap);
				ret_bitmap = _resv_select(
					resv_desc_ptr, resv_select);
				if (!ret_bitmap) {
					bit_or(resv_select->node_bitmap,
					       save_bitmap);
					FREE_NULL_BITMAP(save_bitmap);
				}
			}
			FREE_NULL_BITMAP(tmp_bitmap);
			if (ret_bitmap)
				break;
		}
		list_iterator_destroy(job_iterator);
	}

fini:	FREE_NULL_BITMAP(orig_bitmap);
	FREE_NULL_BITMAP(save_bitmap);

	if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
		char *nodes[2] = {
			(ret_bitmap ? bitmap2node_name(ret_bitmap) : NULL),
			bitmap2node_name(orig_resv_select.node_bitmap),
		};
		char *cores[2] = {0};

		if (resv_select->core_bitmap)
			cores[0] = bit_fmt_full(resv_select->core_bitmap);
		if (orig_resv_select.core_bitmap)
			cores[1] = bit_fmt_full(orig_resv_select.core_bitmap);

		log_flag(RESERVATION, "%s: reservation %s picked nodes:%s cores:%s from possible_nodes:%s used_cores:%s",
			 __func__, resv_desc_ptr->name,
			 ((nodes[0] && nodes[0][0]) ? nodes[0] : "(NONE)"),
			 ((cores[0] && cores[0][0]) ? cores[0] : "(NONE)"),
			 ((nodes[1] && nodes[1][0]) ? nodes[1] : "(NONE)"),
			 ((cores[1] && cores[1][0]) ? cores[1] : "(NONE)"));

		xfree(nodes[0]);
		xfree(nodes[1]);
		xfree(cores[0]);
		xfree(cores[1]);
		_free_resv_select_members(&orig_resv_select);
	}

	return ret_bitmap;
}

/* Determine if a job has access to a reservation
 * RET SLURM_SUCCESS if true, some error code otherwise */
static int _valid_job_access_resv(job_record_t *job_ptr,
				  slurmctld_resv_t *resv_ptr,
				  bool show_security_violation_error)
{
	bool account_good = false, user_good = false;
	int i;

	if (!resv_ptr) {
		info("Reservation name not found (%s)", job_ptr->resv_name);
		return ESLURM_RESERVATION_INVALID;
	}

	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT) {
		verbose("%s: %pJ attempting to use reservation %s with floating start time",
			__func__, job_ptr, resv_ptr->name);
		return ESLURM_RESERVATION_ACCESS;
	}

	if (validate_slurm_user(job_ptr->user_id))
		return SLURM_SUCCESS;

	/* Determine if we have access */
	if (accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS) {
		char tmp_char[30];
		slurmdb_assoc_rec_t *assoc;
		if (!resv_ptr->assoc_list) {
			error("Reservation %s has no association list. "
			      "Checking user/account lists",
			      resv_ptr->name);
			goto no_assocs;
		}

		if (!job_ptr->assoc_ptr) {
			slurmdb_assoc_rec_t assoc_rec;
			/* This should never be called, but just to be
			 * safe we will try to fill it in. */
			memset(&assoc_rec, 0,
			       sizeof(slurmdb_assoc_rec_t));
			assoc_rec.id = job_ptr->assoc_id;
			if (assoc_mgr_fill_in_assoc(
				    acct_db_conn, &assoc_rec,
				    accounting_enforce,
				    (slurmdb_assoc_rec_t **)
				    &job_ptr->assoc_ptr, false))
				goto end_it;
		}

		/* Check to see if the association is here or the parent
		 * association is listed in the valid associations. */
		if (strchr(resv_ptr->assoc_list, '-')) {
			assoc = job_ptr->assoc_ptr;
			while (assoc) {
				snprintf(tmp_char, sizeof(tmp_char), ",-%u,",
					 assoc->id);
				if (xstrstr(resv_ptr->assoc_list, tmp_char))
					goto end_it;	/* explicitly denied */
				assoc = assoc->usage->parent_assoc_ptr;
			}
		}
		if (xstrstr(resv_ptr->assoc_list, ",1") ||
		    xstrstr(resv_ptr->assoc_list, ",2") ||
		    xstrstr(resv_ptr->assoc_list, ",3") ||
		    xstrstr(resv_ptr->assoc_list, ",4") ||
		    xstrstr(resv_ptr->assoc_list, ",5") ||
		    xstrstr(resv_ptr->assoc_list, ",6") ||
		    xstrstr(resv_ptr->assoc_list, ",7") ||
		    xstrstr(resv_ptr->assoc_list, ",8") ||
		    xstrstr(resv_ptr->assoc_list, ",9") ||
		    xstrstr(resv_ptr->assoc_list, ",0")) {
			assoc = job_ptr->assoc_ptr;
			while (assoc) {
				snprintf(tmp_char, sizeof(tmp_char), ",%u,",
					 assoc->id);
				if (xstrstr(resv_ptr->assoc_list, tmp_char))
					return SLURM_SUCCESS;
				assoc = assoc->usage->parent_assoc_ptr;
			}
		} else {
			return SLURM_SUCCESS;
		}
	} else {
no_assocs:	if ((resv_ptr->user_cnt == 0) ||
		    (resv_ptr->ctld_flags & RESV_CTLD_USER_NOT))
			user_good = true;
		for (i = 0; i < resv_ptr->user_cnt; i++) {
			if (job_ptr->user_id == resv_ptr->user_list[i]) {
				if (resv_ptr->ctld_flags & RESV_CTLD_USER_NOT)
					user_good = false;
				else
					user_good = true;
				break;
			}
		}
		if (!user_good)
			goto end_it;
		if ((resv_ptr->user_cnt != 0) && (resv_ptr->account_cnt == 0))
			return SLURM_SUCCESS;

		if ((resv_ptr->account_cnt == 0) ||
		    (resv_ptr->ctld_flags & RESV_CTLD_ACCT_NOT))
			account_good = true;
		for (i=0; (i<resv_ptr->account_cnt) && job_ptr->account; i++) {
			if (resv_ptr->account_list[i] &&
			    (xstrcmp(job_ptr->account,
				    resv_ptr->account_list[i]) == 0)) {
				if (resv_ptr->ctld_flags & RESV_CTLD_ACCT_NOT)
					account_good = false;
				else
					account_good = true;
				break;
			}
		}
		if (!account_good)
			goto end_it;
		return SLURM_SUCCESS;
	}

end_it:
	if (show_security_violation_error)
		info("Security violation, uid=%u account=%s attempt to use reservation %s",
		     job_ptr->user_id, job_ptr->account, resv_ptr->name);

	return ESLURM_RESERVATION_ACCESS;
}

/*
 * Determine if a job can start now based only upon reservations
 *
 * IN job_ptr      - job to test
 * RET	SLURM_SUCCESS if runable now, otherwise an error code
 */
extern int job_test_resv_now(job_record_t *job_ptr)
{
	slurmctld_resv_t * resv_ptr;
	time_t now;
	int rc;

	if (job_ptr->resv_name == NULL)
		return SLURM_SUCCESS;

	if (!job_ptr->resv_ptr) {
		rc = validate_job_resv(job_ptr);
		return rc;
	}
	resv_ptr = job_ptr->resv_ptr;

	rc = _valid_job_access_resv(job_ptr, resv_ptr, true);
	if (rc != SLURM_SUCCESS)
		return rc;

	if (resv_ptr->flags & RESERVE_FLAG_FLEX)
		return SLURM_SUCCESS;

	now = time(NULL);
	if (now < resv_ptr->start_time) {
		/* reservation starts later */
		return ESLURM_INVALID_TIME_VALUE;
	}
	if (now > resv_ptr->end_time) {
		/* reservation ended earlier */
		return ESLURM_RESERVATION_INVALID;
	}
	if ((resv_ptr->node_cnt == 0) &&
	    !(resv_ptr->flags & RESERVE_FLAG_ANY_NODES)) {
		/* empty reservation treated like it will start later */
		return ESLURM_INVALID_TIME_VALUE;
	}

	return SLURM_SUCCESS;
}

/*
 * Note that a job is starting or finishing execution. If that job is associated
 * with a reservation having the "Replace" flag, then remove that job's nodes
 * from the reservation. Additional nodes will be added to the reservation from
 * those currently available.
 */
extern void resv_replace_update(job_record_t *job_ptr)
{
	slurmctld_resv_t *resv_ptr;

	if (job_ptr->resv_name == NULL)
		return;

	if (!job_ptr->resv_ptr)
		/* Don't check for error here, we are ok ignoring it */
		(void)validate_job_resv(job_ptr);

	resv_ptr = job_ptr->resv_ptr;

	if (!resv_ptr || !resv_ptr->node_bitmap ||
	    (!(resv_ptr->ctld_flags & RESV_CTLD_FULL_NODE) &&
	     (resv_ptr->node_cnt > 1)) ||
	    !(resv_ptr->flags & RESERVE_FLAG_REPLACE) ||
	    (resv_ptr->flags & RESERVE_FLAG_SPEC_NODES) ||
	    (resv_ptr->flags & RESERVE_FLAG_STATIC) ||
	    (resv_ptr->flags & RESERVE_FLAG_MAINT))
		return;

	_resv_node_replace(resv_ptr);
}

/*
 * Adjust a job's time_limit and end_time as needed to avoid using
 * reserved resources. Don't go below job's time_min value.
 */
extern void job_time_adj_resv(job_record_t *job_ptr)
{
	list_itr_t *iter;
	slurmctld_resv_t * resv_ptr;
	time_t now = time(NULL);
	int32_t resv_begin_time;

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(iter))) {
		if (resv_ptr->end_time <= now)
			(void)_advance_resv_time(resv_ptr);
		if (job_ptr->resv_ptr == resv_ptr)
			continue;	/* authorized user of reservation */
		if (resv_ptr->start_time <= now)
			continue;	/* already validated */
		if (resv_ptr->start_time >= job_ptr->end_time)
			continue;	/* reservation starts after job ends */
		if (!license_list_overlap(job_ptr->license_list,
					  resv_ptr->license_list) &&
		    ((resv_ptr->node_bitmap == NULL) ||
		     (bit_overlap_any(resv_ptr->node_bitmap,
				      job_ptr->node_bitmap) == 0)))
			continue;	/* disjoint resources */
		resv_begin_time = difftime(resv_ptr->start_time, now) / 60;
		job_ptr->time_limit = MIN(job_ptr->time_limit,resv_begin_time);
	}
	list_iterator_destroy(iter);
	job_ptr->time_limit = MAX(job_ptr->time_limit, job_ptr->time_min);
	job_end_time_reset(job_ptr);
}

/*
 * For a given license_list, return the total count of licenses of the
 * specified name
 */
static int _license_cnt(list_t *license_list, char *lic_name)
{
	int lic_cnt = 0;
	list_itr_t *iter;
	licenses_t *license_ptr;

	if (license_list == NULL)
		return lic_cnt;

	iter = list_iterator_create(license_list);
	while ((license_ptr = list_next(iter))) {
		if (xstrcmp(license_ptr->name, lic_name) == 0)
			lic_cnt += license_ptr->total;
	}
	list_iterator_destroy(iter);

	return lic_cnt;
}

/*
 * get the run time of a job, in seconds
 * job_ptr IN - pointer to the job record
 * reboot IN - true if node reboot required
 */
static uint32_t _get_job_duration(job_record_t *job_ptr, bool reboot)
{
	uint32_t duration;
	uint16_t time_slices = 1;

	if (job_ptr->time_limit == INFINITE)
		duration = YEAR_SECONDS;
	else if (job_ptr->time_limit != NO_VAL)
		duration = (job_ptr->time_limit * 60);
	else {	/* partition time limit */
		if (job_ptr->part_ptr->max_time == INFINITE)
			duration = YEAR_SECONDS;
		else
			duration = (job_ptr->part_ptr->max_time * 60);
	}
	if (job_ptr->part_ptr)
		time_slices = job_ptr->part_ptr->max_share & ~SHARED_FORCE;
	if ((duration != YEAR_SECONDS) && (time_slices > 1) &&
	    (slurm_conf.preempt_mode & PREEMPT_MODE_GANG)) {
		/* FIXME: Ideally we figure out how many jobs are actually
		 * time-slicing on each node rather than using the maximum
		 * value. */
		duration *= time_slices;
	}

	/* FIXME: reboot and sending it to this function needs to be removed */
	/* if (reboot) */
	/* 	duration += node_features_g_boot_time(); */
	return duration;
}

static void _add_bb_resv(burst_buffer_info_msg_t **bb_resv, char *plugin,
			 char *type, uint64_t cnt)
{
	burst_buffer_info_t *bb_array;
	burst_buffer_pool_t *pool_ptr;
	int i;

	if (*bb_resv == NULL)
		*bb_resv = xmalloc(sizeof(burst_buffer_info_msg_t));

	for (i = 0, bb_array = (*bb_resv)->burst_buffer_array;
	     i < (*bb_resv)->record_count; i++) {
		if (!xstrcmp(plugin, bb_array->name))
			break;
	}
	if (i >= (*bb_resv)->record_count) {
		(*bb_resv)->record_count++;
		(*bb_resv)->burst_buffer_array = xrealloc(
			(*bb_resv)->burst_buffer_array,
			sizeof(burst_buffer_info_t) * (*bb_resv)->record_count);
		bb_array = (*bb_resv)->burst_buffer_array +
			   (*bb_resv)->record_count - 1;
		bb_array->name = xstrdup(plugin);
	}

	if (type == NULL) {
		bb_array->used_space += cnt;
		return;
	}

	for (i = 0, pool_ptr = bb_array->pool_ptr; i < bb_array->pool_cnt; i++){
		if ((pool_ptr->name == NULL) || !xstrcmp(type, pool_ptr->name))
			break;
	}
	if (i >= bb_array->pool_cnt) {
		bb_array->pool_cnt++;
		bb_array->pool_ptr = xrealloc(bb_array->pool_ptr,
					      sizeof(burst_buffer_pool_t) *
					      bb_array->pool_cnt);
		pool_ptr = bb_array->pool_ptr + bb_array->pool_cnt - 1;
		pool_ptr->name = xstrdup(type);
	}
	pool_ptr->used_space += cnt;
}

static void _update_bb_resv(burst_buffer_info_msg_t **bb_resv, char *bb_spec)
{
	uint64_t cnt, mult;
	char *end_ptr = NULL, *unit = NULL;
	char *sep, *tmp_spec, *tok, *plugin, *type;

	if ((bb_spec == NULL) || (bb_spec[0] == '\0'))
		return;

	tmp_spec = xstrdup(bb_spec);
	tok = strtok_r(tmp_spec, ",", &end_ptr);
	while (tok) {
		if (!xstrncmp(tok, "datawarp:", 9)) {
			plugin = "datawarp";
			tok +=9;
		} else if (!xstrncmp(tok, "generic:", 8)) {
			plugin = "generic";
			tok += 8;
		} else
			plugin = NULL;

		sep = strchr(tok, ':');
		if (sep) {
			type = tok;
			sep[0] = '\0';
			tok = sep + 1;
		} else {
			type = NULL;
		}

		cnt = (uint64_t) strtoull(tok, &unit, 10);
		if (!xstrcasecmp(unit, "n") ||
		    !xstrcasecmp(unit, "node") ||
		    !xstrcasecmp(unit, "nodes")) {
			type = "nodes";	/* Cray node spec format */
		} else if ((mult = suffix_mult(unit)) != NO_VAL64) {
			cnt *= mult;
		}

		if (cnt)
			_add_bb_resv(bb_resv, plugin, type, cnt);
		tok = strtok_r(NULL, ",", &end_ptr);
	}
	xfree(tmp_spec);
}

/*
 * Determine how many burst buffer resources the specified job is prevented
 *	from using due to reservations
 *
 * IN job_ptr   - job to test
 * IN when      - when the job is expected to start
 * IN reboot    - true if node reboot required to start job
 * RET burst buffer reservation structure, call
 *	 slurm_free_burst_buffer_info_msg() to free
 */
extern burst_buffer_info_msg_t *job_test_bb_resv(job_record_t *job_ptr,
						 time_t when, bool reboot)
{
	slurmctld_resv_t * resv_ptr;
	time_t job_start_time, job_end_time, now = time(NULL);
	time_t job_end_time_use;
	burst_buffer_info_msg_t *bb_resv = NULL;
	list_itr_t *iter;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return bb_resv;

	job_start_time = when;
	job_end_time   = when + _get_job_duration(job_ptr, reboot);
	iter = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(iter))) {
		if (resv_ptr->end_time <= now)
			(void)_advance_resv_time(resv_ptr);

		if (reboot)
			job_end_time_use =
				job_end_time + resv_ptr->boot_time;
		else
			job_end_time_use = job_end_time;

		if ((resv_ptr->start_time >= job_end_time_use) ||
		    (resv_ptr->end_time   <= job_start_time))
			continue;	/* reservation at different time */
		if ((resv_ptr->burst_buffer == NULL) ||
		    (resv_ptr->burst_buffer[0] == '\0'))
			continue;	/* reservation has no burst buffers */
		if (!xstrcmp(job_ptr->resv_name, resv_ptr->name))
			continue;	/* job can use this reservation */

		_update_bb_resv(&bb_resv, resv_ptr->burst_buffer);
	}
	list_iterator_destroy(iter);

	return bb_resv;
}

/*
 * Determine how many licenses of the give type the specified job is
 *	prevented from using due to reservations
 *
 * IN job_ptr   - job to test
 * IN lic_name  - name of license
 * IN when      - when the job is expected to start
 * IN reboot    - true if node reboot required to start job
 * RET number of licenses of this type the job is prevented from using
 */
extern int job_test_lic_resv(job_record_t *job_ptr, char *lic_name,
			     time_t when, bool reboot)
{
	slurmctld_resv_t * resv_ptr;
	time_t job_start_time, job_end_time, now = time(NULL);
	time_t job_end_time_use;
	list_itr_t *iter;
	int resv_cnt = 0;

	job_start_time = when;
	job_end_time   = when + _get_job_duration(job_ptr, reboot);
	iter = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(iter))) {
		if (resv_ptr->end_time <= now)
			(void)_advance_resv_time(resv_ptr);

		if (reboot)
			job_end_time_use =
				job_end_time + resv_ptr->boot_time;
		else
			job_end_time_use = job_end_time;

		if ((resv_ptr->start_time >= job_end_time_use) ||
		    (resv_ptr->end_time   <= job_start_time))
			continue;	/* reservation at different time */

		if (job_ptr->resv_name &&
		    (xstrcmp(job_ptr->resv_name, resv_ptr->name) == 0))
			continue;	/* job can use this reservation */

		resv_cnt += _license_cnt(resv_ptr->license_list, lic_name);
	}
	list_iterator_destroy(iter);

	/* info("%pJ blocked from %d licenses of type %s",
	     job_ptr, resv_cnt, lic_name); */
	return resv_cnt;
}

static void _get_rel_start_end(slurmctld_resv_t *resv_ptr, time_t now,
			       time_t *start_relative, time_t *end_relative)
{
	xassert(resv_ptr);
	xassert(start_relative);
	xassert(end_relative);

	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT) {
		*start_relative = resv_ptr->start_time + now;
		if (resv_ptr->duration == INFINITE)
			*end_relative = *start_relative + YEAR_SECONDS;
		else if (resv_ptr->duration && (resv_ptr->duration != NO_VAL)) {
			*end_relative =
				*start_relative + resv_ptr->duration * 60;
		} else {
			*end_relative = resv_ptr->end_time;
			if (*start_relative > *end_relative)
				*start_relative = *end_relative;
		}
	} else {
		if (resv_ptr->end_time <= now)
			(void)_advance_resv_time(resv_ptr);
		*start_relative = resv_ptr->start_time_first;
		*end_relative = resv_ptr->end_time;
	}
}

extern int job_test_resv(job_record_t *job_ptr, time_t *when,
			 bool move_time, bitstr_t **node_bitmap,
			 resv_exc_t *resv_exc_ptr, bool *resv_overlap,
			 bool reboot)
{
	slurmctld_resv_t *resv_ptr = NULL, *res2_ptr;
	time_t job_start_time, job_end_time, job_end_time_use, lic_resv_time;
	time_t start_relative, end_relative;
	time_t now = time(NULL);
	list_itr_t *iter;
	int i, rc = SLURM_SUCCESS, rc2;

	*resv_overlap = false;	/* initialize to false */
	job_start_time = *when;
	job_end_time   = *when + _get_job_duration(job_ptr, reboot);
	*node_bitmap = (bitstr_t *) NULL;

	if (job_ptr->resv_name) {
		if (!job_ptr->resv_ptr) {
			rc2 = validate_job_resv(job_ptr);
			if (rc2 != SLURM_SUCCESS)
				return rc2;
		}
		resv_ptr = job_ptr->resv_ptr;

		rc2 = _valid_job_access_resv(job_ptr, resv_ptr, true);
		if (rc2 != SLURM_SUCCESS)
			return rc2;
		/*
		 * Just in case the reservation was altered since last looking
		 * we want to make sure things are good in the database.
		 */
		if (job_ptr->resv_id != resv_ptr->resv_id) {
			job_ptr->resv_id = resv_ptr->resv_id;
			/*
			 * Update the database if not using a magnetic
			 * reservation
			 */
			if (!(job_ptr->bit_flags & JOB_MAGNETIC))
				jobacct_storage_g_job_start(
					acct_db_conn, job_ptr);
		}
		if (resv_ptr->flags & RESERVE_FLAG_FLEX) {
			/* Job not bound to reservation nodes or time */
			*node_bitmap = node_conf_get_active_bitmap();
		} else {
			if (resv_ptr->end_time <= now)
				(void)_advance_resv_time(resv_ptr);
			if (*when < resv_ptr->start_time) {
				/* reservation starts later */
				*when = resv_ptr->start_time;
				return ESLURM_INVALID_TIME_VALUE;
			}
			if ((resv_ptr->node_cnt == 0) &&
			    (!(resv_ptr->flags & RESERVE_FLAG_ANY_NODES))) {
				/*
				 * empty reservation treated like it will
				 * start later
				 */
				*when = now + 600;
				return ESLURM_INVALID_TIME_VALUE;
			}
			if (*when > resv_ptr->end_time) {
				/* reservation ended earlier */
				*when = resv_ptr->end_time;
				if ((now > resv_ptr->end_time) ||
				    ((job_ptr->details) &&
				     (job_ptr->details->begin_time >
				      resv_ptr->end_time))) {
					debug("%s: Holding %pJ, expired reservation %s",
					      __func__, job_ptr, resv_ptr->name);
					job_ptr->priority = 0;	/* admin hold */
				}
				return ESLURM_RESERVATION_INVALID;
			}
			if (job_ptr->details->req_node_bitmap &&
			    (!(resv_ptr->flags & RESERVE_FLAG_ANY_NODES)) &&
			    !bit_super_set(job_ptr->details->req_node_bitmap,
					   resv_ptr->node_bitmap)) {
				return ESLURM_RESERVATION_INVALID;
			}
			if (resv_ptr->flags & RESERVE_FLAG_ANY_NODES) {
				*node_bitmap = node_conf_get_active_bitmap();
			} else {
				*node_bitmap = bit_copy(resv_ptr->node_bitmap);
			}
		}
		/* The job can only run on nodes in the partition */
		bit_and(*node_bitmap, job_ptr->part_ptr->node_bitmap);

		/*
		 * if there are any overlapping reservations, we need to
		 * prevent the job from using those nodes (e.g. MAINT nodes)
		 */
		iter = list_iterator_create(resv_list);
		while ((res2_ptr = list_next(iter))) {
			if (reboot)
				job_end_time_use =
					job_end_time + res2_ptr->boot_time;
			else
				job_end_time_use = job_end_time;

			_get_rel_start_end(
				res2_ptr, now, &start_relative, &end_relative);

			if ((resv_ptr->flags & RESERVE_FLAG_MAINT) ||
			    ((resv_ptr->flags & RESERVE_FLAG_OVERLAP) &&
			     !(res2_ptr->flags & RESERVE_FLAG_MAINT)) ||
			    (res2_ptr == resv_ptr) ||
			    (res2_ptr->node_bitmap == NULL) ||
			    (start_relative >= job_end_time_use) ||
			    (end_relative   <= job_start_time) ||
			    (!(res2_ptr->ctld_flags & RESV_CTLD_FULL_NODE)))
				continue;
			if (bit_overlap_any(*node_bitmap,
					    res2_ptr->node_bitmap)) {
				log_flag(RESERVATION, "%s: reservation %s overlaps %s with %u nodes",
					 __func__, resv_ptr->name,
					 res2_ptr->name,
					 bit_overlap(*node_bitmap,
						     res2_ptr->node_bitmap));
				*resv_overlap = true;
				bit_and_not(*node_bitmap,res2_ptr->node_bitmap);
			}
		}
		list_iterator_destroy(iter);

		if (slurm_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
			char *nodes = bitmap2node_name(*node_bitmap);
			verbose("%s: %pJ reservation:%s nodes:%s",
				__func__, job_ptr, job_ptr->resv_name, nodes);
			xfree(nodes);
		}

		/*
		 * if reservation is using just partial nodes, this returns
		 * coremap to exclude
		 */
		if (resv_ptr->core_bitmap && resv_exc_ptr &&
		    !(resv_ptr->flags & RESERVE_FLAG_FLEX) ) {
			free_core_array(&resv_exc_ptr->exc_cores);
			resv_exc_ptr->core_bitmap =
				bit_copy(resv_ptr->core_bitmap);
			bit_not(resv_exc_ptr->core_bitmap);
			resv_exc_ptr->exc_cores =
				core_bitmap_to_array(resv_exc_ptr->core_bitmap);
			resv_exc_ptr->gres_list_inc =
				gres_job_state_list_dup(
					resv_ptr->gres_list_alloc);
			resv_exc_ptr->gres_list_exc = NULL;
			resv_exc_ptr->gres_js_exc = NULL;
			resv_exc_ptr->gres_js_inc = NULL;
		}

		return SLURM_SUCCESS;
	}

	job_ptr->resv_ptr = NULL;	/* should be redundant */
	*node_bitmap = node_conf_get_active_bitmap();
	if (list_count(resv_list) == 0)
		return SLURM_SUCCESS;

	/*
	 * Job has no reservation, try to find time when this can
	 * run and get it's required nodes (if any)
	 */
	for (i = 0; ; i++) {
		lic_resv_time = (time_t) 0;

		iter = list_iterator_create(resv_list);
		while ((resv_ptr = list_next(iter))) {
			_get_rel_start_end(
				resv_ptr, now, &start_relative, &end_relative);

			if (reboot)
				job_end_time_use =
					job_end_time + resv_ptr->boot_time;
			else
				job_end_time_use = job_end_time;

			if ((start_relative >= job_end_time_use) ||
			    (end_relative <= job_start_time))
				continue;
			/*
			 * FIXME: This only tracks when ANY licenses required
			 * by the job are freed by any reservation without
			 * counting them, so the results are not accurate.
			 */
			if (license_list_overlap(job_ptr->license_list,
						 resv_ptr->license_list)) {
				if ((lic_resv_time == (time_t) 0) ||
				    (lic_resv_time > resv_ptr->end_time))
					lic_resv_time = resv_ptr->end_time;
			}

			if (resv_ptr->node_bitmap == NULL)
				continue;
			/*
			 * Check if we are able to use this reservation's
			 * resources even though we didn't request it.
			 */
			if (resv_ptr->max_start_delay &&
			    (job_ptr->warn_time <= resv_ptr->max_start_delay) &&
			    (job_ptr->warn_flags & KILL_JOB_RESV)) {
				continue;
			}

			if (resv_ptr->flags & RESERVE_FLAG_ALL_NODES ||
			    ((resv_ptr->flags  & RESERVE_FLAG_PART_NODES) &&
			     job_ptr->part_ptr == resv_ptr->part_ptr) ||
			    ((resv_ptr->flags & RESERVE_FLAG_MAINT) &&
			     job_ptr->part_ptr &&
			     (bit_super_set(job_ptr->part_ptr->node_bitmap,
					    resv_ptr->node_bitmap)))) {
				rc = ESLURM_RESERVATION_MAINT;
				if (move_time)
					*when = resv_ptr->end_time;
				break;
			}

			if (job_ptr->details->req_node_bitmap &&
			    bit_overlap_any(job_ptr->details->req_node_bitmap,
					    resv_ptr->node_bitmap) &&
			    (!resv_ptr->tres_str ||
			     job_ptr->details->whole_node &
			     WHOLE_NODE_REQUIRED)) {
				if (move_time)
					*when = resv_ptr->end_time;
				rc = ESLURM_NODES_BUSY;
				break;
			}

			if (IS_JOB_WHOLE_TOPO(job_ptr)) {
				bitstr_t *efctv_bitmap =
					bit_copy(resv_ptr->node_bitmap);
				topology_g_whole_topo(efctv_bitmap);

				log_flag(RESERVATION, "%s: %pJ will can not share topology with %s",
					 __func__, job_ptr, resv_ptr->name);
				bit_and_not(*node_bitmap, efctv_bitmap);
				FREE_NULL_BITMAP(efctv_bitmap);

			} else if ((resv_ptr->ctld_flags &
				    RESV_CTLD_FULL_NODE) ||
				  (job_ptr->details->whole_node &
				   WHOLE_NODE_REQUIRED)) {
				log_flag(RESERVATION, "%s: reservation %s uses full nodes or %pJ will not share nodes",
					 __func__, resv_ptr->name, job_ptr);
				bit_and_not(*node_bitmap, resv_ptr->node_bitmap);
			} else {
				log_flag(RESERVATION, "%s: reservation %s uses partial nodes",
				     __func__, resv_ptr->name);

				if (resv_ptr->core_bitmap == NULL) {
					;
				} else if (!resv_exc_ptr) {
					error("%s: resv_exc_ptr is NULL",
					      __func__);
				} else if (!resv_exc_ptr->core_bitmap) {
					resv_exc_ptr->core_bitmap =
						bit_copy(resv_ptr->core_bitmap);
				} else {
					bit_or(resv_exc_ptr->core_bitmap,
					       resv_ptr->core_bitmap);
				}
			}

			if (resv_exc_ptr)
				_addto_gres_list_exc(
					&resv_exc_ptr->gres_list_exc,
					resv_ptr->gres_list_alloc);

			if(!job_ptr->part_ptr ||
			    bit_overlap_any(job_ptr->part_ptr->node_bitmap,
					    resv_ptr->node_bitmap)) {
				*resv_overlap = true;
				continue;
			}
		}
		list_iterator_destroy(iter);

		if (resv_exc_ptr) {
			free_core_array(&resv_exc_ptr->exc_cores);
			if (resv_exc_ptr->core_bitmap) {
				resv_exc_ptr->exc_cores = core_bitmap_to_array(
					resv_exc_ptr->core_bitmap);
			}
		}

		if ((rc == SLURM_SUCCESS) && move_time) {
			if (license_job_test(job_ptr, job_start_time, reboot)
			    == EAGAIN) {
				/*
				 * Need to postpone for licenses. Time returned
				 * is best case; first reservation with those
				 * licenses ends.
				 */
				rc = ESLURM_NODES_BUSY;
				if (lic_resv_time > *when)
					*when = lic_resv_time;
			}
		}
		if (rc == SLURM_SUCCESS)
			break;
		/*
		 * rc == ESLURM_NODES_BUSY or rc == ESLURM_RESERVATION_MAINT
		 * above "break"
		 */
		if (move_time && (i < 10)) {  /* Retry for later start time */
			job_start_time = *when;
			job_end_time   = *when +
					 _get_job_duration(job_ptr, reboot);
			node_conf_set_all_active_bits(*node_bitmap);
			rc = SLURM_SUCCESS;
			continue;
		}
		FREE_NULL_BITMAP(*node_bitmap);
		break;	/* Give up */
	}

	return rc;
}

static int _update_resv_group_uid_access_list(void *x, void *arg)
{
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *)x;
	int *updated = (int *)arg;
	int user_cnt = 0;
	uid_t *tmp_uids;

	if (!resv_ptr->groups)
		return 0;

	tmp_uids = get_groups_members(resv_ptr->groups, &user_cnt);

	/*
	 * If the lists are different sizes clearly we are different.
	 * If the memory isn't the same they are different as well
	 * as the lists will be in the same order.
	 */
	if ((resv_ptr->user_cnt != user_cnt) ||
	    memcmp(tmp_uids, resv_ptr->user_list,
		   sizeof(*tmp_uids) * user_cnt)) {
		char *old_assocs = xstrdup(resv_ptr->assoc_list);

		resv_ptr->user_cnt = user_cnt;
		xfree(resv_ptr->user_list);
		resv_ptr->user_list = tmp_uids;
		tmp_uids = NULL;

		/* Now update the associations to match */
		(void)_set_assoc_list(resv_ptr);

		/* Now see if something really did change */
		if (!slurm_with_slurmdbd() ||
		    xstrcmp(old_assocs, resv_ptr->assoc_list))
			*updated = 1;
		xfree(old_assocs);
	}

	xfree(tmp_uids);

	return 0;
}

/*
 * Determine the time of the first reservation to end after some time.
 * return zero of no reservation ends after that time.
 * IN start_time - look for reservations ending after this time
 * IN resolution - return end_time with the given resolution, this is important
 * to avoid additional try_later attempts from backfill when we have multiple
 * reservations with very close end time.
 * RET the reservation end time or zero of none found
 */
extern time_t find_resv_end(time_t start_time, int resolution)
{
	list_itr_t *iter;
	slurmctld_resv_t *resv_ptr;
	time_t end_time = 0;

	if (!resv_list)
		return end_time;

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(iter))) {
		if (start_time > resv_ptr->end_time)
			continue;
		if ((end_time == 0) || (resv_ptr->end_time < end_time))
			end_time = resv_ptr->end_time;
	}
	list_iterator_destroy(iter);

	/* Round-up returned time to given resolution */
	if (resolution > 0) {
		end_time = ROUNDUP(end_time, resolution);
		end_time *= resolution;
	}

	return end_time;
}

/* Test a particular job for valid reservation
 * and refill job_run_cnt/job_pend_cnt */
static int _job_resv_check(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;

	if (!job_ptr->resv_ptr && !job_ptr->resv_list)
		return SLURM_SUCCESS;

	if (IS_JOB_PENDING(job_ptr)) {
		if (job_ptr->resv_list) {
			list_for_each(job_ptr->resv_list, _update_resv_pend_cnt,
				      NULL);
		} else {
			xassert(job_ptr->resv_ptr->magic == RESV_MAGIC);
			job_ptr->resv_ptr->job_pend_cnt++;
		}
	} else if (!IS_JOB_FINISHED(job_ptr) && job_ptr->resv_ptr) {
		xassert(job_ptr->resv_ptr->magic == RESV_MAGIC);
		job_ptr->resv_ptr->job_run_cnt++;
	}

	return SLURM_SUCCESS;
}

static int _set_job_resvid(void *object, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) object;
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *)arg;

	if ((job_ptr->resv_ptr != resv_ptr) || !IS_JOB_PENDING(job_ptr))
		return SLURM_SUCCESS;

	log_flag(RESERVATION, "updating %pJ to correct resv_id (%u->%u) of reoccurring reservation '%s'",
		 job_ptr, job_ptr->resv_id, resv_ptr->resv_id, resv_ptr->name);
	job_ptr->resv_id = resv_ptr->resv_id;
	/* Update the database */
	jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	return SLURM_SUCCESS;
}

static void *_update_resv_jobs(void *arg)
{
	slurmctld_resv_t *resv_ptr;
	uint32_t resv_id = *(uint32_t *)arg;
	/* get the job write lock and node and config read lock */
	slurmctld_lock_t job_write_lock = {
		.conf = READ_LOCK,
		.job = WRITE_LOCK,
		.node = READ_LOCK,
	};

	lock_slurmctld(job_write_lock);
	if (!resv_list) {
		unlock_slurmctld(job_write_lock);
		return NULL;
	}

	resv_ptr = list_find_first(resv_list, _find_resv_id, &resv_id);

	if (!resv_ptr) {
		unlock_slurmctld(job_write_lock);
		return NULL;
	}

	list_for_each(job_list, _set_job_resvid, resv_ptr);
	unlock_slurmctld(job_write_lock);

	return NULL;
}


/* Advance a expired reservation's time stamps one day or one week
 * as appropriate. */
static int _advance_resv_time(slurmctld_resv_t *resv_ptr)
{
	time_t now;
	struct tm tm;
	int day_cnt = 0, hour_cnt = 0;
	int rc = SLURM_ERROR;
	/* Make sure we have node write locks. */
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));

	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT)
		return rc;		/* Not applicable */

	if (resv_ptr->flags & RESERVE_FLAG_HOURLY) {
		hour_cnt = 1;
	} else if (resv_ptr->flags & RESERVE_FLAG_DAILY) {
		day_cnt = 1;
	} else if (resv_ptr->flags & RESERVE_FLAG_WEEKDAY) {
		now = time(NULL);
		localtime_r(&now, &tm);
		if (tm.tm_wday == 5)		/* Friday */
			day_cnt = 3;
		else if (tm.tm_wday == 6)	/* Saturday */
			day_cnt = 2;
		else
			day_cnt = 1;
	} else if (resv_ptr->flags & RESERVE_FLAG_WEEKEND) {
		now = time(NULL);
		localtime_r(&now, &tm);
		if (tm.tm_wday == 0)		/* Sunday */
			day_cnt = 6;
		else if (tm.tm_wday == 6)	/* Saturday */
			day_cnt = 1;
		else
			day_cnt = 6 - tm.tm_wday;
	} else if (resv_ptr->flags & RESERVE_FLAG_WEEKLY) {
		day_cnt = 7;
	}

	if (day_cnt || hour_cnt) {
		char *tmp_str = NULL;

		if (!(resv_ptr->ctld_flags & RESV_CTLD_PROLOG))
			_run_script(slurm_conf.resv_prolog, resv_ptr,
				    "ResvProlog");
		if (!(resv_ptr->ctld_flags & RESV_CTLD_EPILOG))
			_run_script(slurm_conf.resv_epilog, resv_ptr,
				    "ResvEpilog");

		/*
		 * Repeated reservations need a new reservation id. Try to get a
		 * new one and update the ID if successful.
		 */
		if (_generate_resv_id()) {
			error("%s, Recurring reservation %s is being "
			      "rescheduled but has the same ID.",
			      __func__, resv_ptr->name);
		} else {
			resv_ptr->resv_id = top_suffix;
			/*
			 * Update pending jobs for this reservation with the new
			 * reservation ID out of band.
			 */
			slurm_thread_create_detached(_update_resv_jobs,
						     &resv_ptr->resv_id);
		}

		if (hour_cnt)
			xstrfmtcat(tmp_str, "%d hour%s",
				   hour_cnt, ((hour_cnt > 1) ? "s" : ""));
		else if (day_cnt)
			xstrfmtcat(tmp_str, "%d day%s",
				   day_cnt, ((day_cnt > 1) ? "s" : ""));
		verbose("%s: reservation %s advanced by %s",
			__func__, resv_ptr->name, tmp_str);
		xfree(tmp_str);

		resv_ptr->idle_start_time = 0;
		resv_ptr->start_time = resv_ptr->start_time_first;
		_advance_time(&resv_ptr->start_time, day_cnt, hour_cnt);
		resv_ptr->start_time_prev = resv_ptr->start_time;
		resv_ptr->start_time_first = resv_ptr->start_time;
		_advance_time(&resv_ptr->end_time, day_cnt, hour_cnt);
		resv_ptr->ctld_flags &= (~RESV_CTLD_PROLOG);
		resv_ptr->ctld_flags &= (~RESV_CTLD_EPILOG);
		_post_resv_create(resv_ptr);
		last_resv_update = time(NULL);
		schedule_resv_save();
		rc = SLURM_SUCCESS;
	} else {
		log_flag(RESERVATION, "%s: skipping reservation %s for being advanced in time",
			 __func__, resv_ptr->name);
	}
	return rc;
}

static void _run_script(char *script, slurmctld_resv_t *resv_ptr, char *name)
{
	uint32_t argc = 2;
	char **argv;

	if (!script || !script[0])
		return;
	if (access(script, X_OK) < 0) {
		error("Invalid ResvProlog or ResvEpilog(%s): %m", script);
		return;
	}
	argv = xcalloc(argc + 1, sizeof(*argv)); /* +1 to NULL-terminate */
	argv[0] = script;
	argv[1] = resv_ptr->name;

	slurmscriptd_run_resv(script, argc, argv,
			      slurm_conf.prolog_epilog_timeout, name);

	xfree(argv);
}

static int _resv_list_reset_cnt(void *x, void *arg)
{
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) x;

	resv_ptr->job_pend_cnt = 0;
	resv_ptr->job_run_cnt  = 0;

	return 0;
}

/* Finish scan of all jobs for valid reservations
 *
 * Purge vestigial reservation records.
 * Advance daily or weekly reservations that are no longer
 *	being actively used.
 */
extern void job_resv_check(void)
{
	list_itr_t *iter;
	slurmctld_resv_t *resv_ptr;
	time_t now = time(NULL);

	if (!resv_list)
		return;

	list_for_each(resv_list, _resv_list_reset_cnt, NULL);
	list_for_each(job_list, _job_resv_check, NULL);

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(iter))) {
		if (resv_ptr->start_time <= now) {
			if (resv_ptr->job_run_cnt || resv_ptr->job_pend_cnt) {
				if ((resv_ptr->flags & RESERVE_FLAG_PURGE_COMP)
				    && resv_ptr->idle_start_time)
					log_flag(RESERVATION, "Resetting idle start time to zero on PURGE_COMP reservation %s due to active associated jobs",
						 resv_ptr->name);
				resv_ptr->idle_start_time = 0;
			} else if (!resv_ptr->idle_start_time) {
				if (resv_ptr->flags & RESERVE_FLAG_PURGE_COMP)
					log_flag(RESERVATION, "Marking idle start time to now on PURGE_COMP reservation %s",
						 resv_ptr->name);
				resv_ptr->idle_start_time = now;
			}
		}

		if ((resv_ptr->flags & RESERVE_FLAG_PURGE_COMP) &&
		    resv_ptr->idle_start_time &&
		    (resv_ptr->end_time > now) &&
		    (resv_ptr->purge_comp_time <=
		     (now - resv_ptr->idle_start_time))) {
			char tmp_pct[40];
			secs2time_str(resv_ptr->purge_comp_time,
				      tmp_pct, sizeof(tmp_pct));
			info("Reservation %s has no more jobs for %s, ending it",
			     resv_ptr->name, tmp_pct);

			(void)_post_resv_delete(resv_ptr);

			/*
			 * If we are ending a reoccurring reservation advance
			 * it, otherwise delete it.
			 */
			if (!(resv_ptr->flags & RESERVE_REOCCURRING)) {
				/*
				 * Reset time here for reoccurring reservations
				 * so we don't continually keep running this.
				 */
				resv_ptr->idle_start_time = 0;

				if (!(resv_ptr->ctld_flags & RESV_CTLD_PROLOG))
					_run_script(slurm_conf.resv_prolog,
						    resv_ptr, "ResvProlog");
				if (!(resv_ptr->ctld_flags & RESV_CTLD_EPILOG))
					_run_script(slurm_conf.resv_epilog,
						    resv_ptr, "ResvEpilog");
				/*
				 * Clear resv ptrs on finished jobs still
				 * pointing to this reservation.
				 */
				_clear_job_resv(resv_ptr);
				list_delete_item(iter);
			} else if (resv_ptr->start_time <= now) {
				_advance_resv_time(resv_ptr);
			}

			last_resv_update = now;
			schedule_resv_save();
			continue;
		}
		if ((resv_ptr->end_time >= now) ||
		    (resv_ptr->duration && (resv_ptr->duration != NO_VAL) &&
		     (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT))) {
			_validate_node_choice(resv_ptr);
			continue;
		}
		if (!(resv_ptr->ctld_flags & RESV_CTLD_PROLOG) ||
		    !(resv_ptr->ctld_flags & RESV_CTLD_EPILOG))
			continue;
		(void)_advance_resv_time(resv_ptr);
		if ((!resv_ptr->job_run_cnt ||
		     (resv_ptr->flags & RESERVE_FLAG_FLEX)) &&
		    !(resv_ptr->flags & RESERVE_REOCCURRING)) {
			if (resv_ptr->job_pend_cnt) {
				info("Purging vestigial reservation %s "
				     "with %u pending jobs",
				     resv_ptr->name, resv_ptr->job_pend_cnt);
			} else {
				debug("Purging vestigial reservation %s",
				      resv_ptr->name);
			}
			_clear_job_resv(resv_ptr);
			list_delete_item(iter);
			last_resv_update = now;
			schedule_resv_save();
		}
	}
	list_iterator_destroy(iter);
}

/*
 * Send all reservations to accounting.  Only needed at first registration
 */
extern int send_resvs_to_accounting(int db_rc)
{
	list_itr_t *itr = NULL;
	slurmctld_resv_t *resv_ptr;
	slurmctld_lock_t node_write_lock = {
		.node = WRITE_LOCK,
		.part = READ_LOCK,
	};

	if (!resv_list)
		return SLURM_SUCCESS;

	lock_slurmctld(node_write_lock);

	itr = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(itr))) {
		if (db_rc == ACCOUNTING_FIRST_REG)
			_post_resv_create(resv_ptr);
		else if (db_rc == ACCOUNTING_NODES_CHANGE_DB) {
			/*
			 * This makes it so we always get the correct node
			 * indexes in the database.
			 */
			slurmctld_resv_t tmp_resv = {0};
			_post_resv_update(resv_ptr, &tmp_resv);
		} else {
			error("%s: unknown db_rc %d", __func__, db_rc);
			break;
		}
	}
	list_iterator_destroy(itr);

	unlock_slurmctld(node_write_lock);

	return SLURM_SUCCESS;
}

/*
 * Set or clear NODE_STATE_MAINT for node_state as needed
 * IN reset_all - if true, then re-initialize all node information for all
 *	reservations, but do not run any prologs or epilogs or count started
 *	reservations
 * RET count of newly started reservations
 */
extern int set_node_maint_mode(bool reset_all)
{
	int i, res_start_cnt = 0;
	node_record_t *node_ptr;
	uint32_t flags;
	list_itr_t *iter;
	slurmctld_resv_t *resv_ptr;
	time_t now = time(NULL);

	if (!resv_list)
		return res_start_cnt;

	flags = NODE_STATE_RES;
	if (reset_all)
		flags |= NODE_STATE_MAINT;
	for (i = 0; (node_ptr = next_node(&i)); i++) {
		node_ptr->node_state &= (~flags);
		xfree(node_ptr->resv_name);
	}

	if (!reset_all) {
		/* NODE_STATE_RES already cleared above,
		 * clear RESERVE_FLAG_MAINT for expired reservations */
		iter = list_iterator_create(resv_list);
		while ((resv_ptr = list_next(iter))) {
			if ((resv_ptr->ctld_flags & RESV_CTLD_NODE_FLAGS_SET) &&
			    (resv_ptr->flags & RESERVE_FLAG_MAINT) &&
			    ((now <  resv_ptr->start_time) ||
			     (now >= resv_ptr->end_time  ))) {
				flags = NODE_STATE_MAINT;
				resv_ptr->ctld_flags &=
					(~RESV_CTLD_NODE_FLAGS_SET);
				_set_nodes_flags(resv_ptr, now, flags,
						 reset_all);
				last_node_update = now;
			}
		}
		list_iterator_destroy(iter);
	}

	/* Set NODE_STATE_RES and possibly NODE_STATE_MAINT for nodes in all
	 * currently active reservations */
	iter = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(iter))) {
		if ((now >= resv_ptr->start_time) &&
		    (now <  resv_ptr->end_time  )) {
			flags = NODE_STATE_RES;
			if (resv_ptr->flags & RESERVE_FLAG_MAINT)
				flags |= NODE_STATE_MAINT;
			resv_ptr->ctld_flags |= RESV_CTLD_NODE_FLAGS_SET;
			_set_nodes_flags(resv_ptr, now, flags, reset_all);
			last_node_update = now;
		}

		if (reset_all)	/* Defer reservation prolog/epilog */
			continue;
		if ((resv_ptr->start_time <= now) &&
		    !(resv_ptr->ctld_flags & RESV_CTLD_PROLOG)) {
			res_start_cnt++;
			resv_ptr->ctld_flags |= RESV_CTLD_PROLOG;
			_run_script(slurm_conf.resv_prolog, resv_ptr,
				    "ResvProlog");
		}
		if ((resv_ptr->end_time <= now) &&
		    !(resv_ptr->ctld_flags & RESV_CTLD_EPILOG)) {
			resv_ptr->ctld_flags |= RESV_CTLD_EPILOG;
			_run_script(slurm_conf.resv_epilog, resv_ptr,
				    "ResvEpilog");
		}
	}
	list_iterator_destroy(iter);

	return res_start_cnt;
}

/* checks if node within node_record_table_ptr is in maint reservation */
extern bool is_node_in_maint_reservation(int nodenum)
{
	bool res = false;
	list_itr_t *iter;
	slurmctld_resv_t *resv_ptr;
	time_t t;

	if (nodenum < 0 || nodenum >= node_record_count || !resv_list)
		return false;

	t = time(NULL);
	iter = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(iter))) {
		if ((resv_ptr->flags & RESERVE_FLAG_MAINT) == 0)
			continue;
		if (! (t >= resv_ptr->start_time
		       && t <= resv_ptr->end_time))
			continue;
		if (resv_ptr->node_bitmap &&
		    bit_test(resv_ptr->node_bitmap, nodenum)) {
			res = true;
			break;
		}
	}
	list_iterator_destroy(iter);

	return res;
}

extern void update_assocs_in_resvs(void)
{
	slurmctld_resv_t *resv_ptr = NULL;
	list_itr_t *iter = NULL;
	slurmctld_lock_t node_write_lock = {
		.node = WRITE_LOCK,
		.part = READ_LOCK,
	};

	if (!resv_list) {
		error("No reservation list given for updating associations");
		return;
	}

	lock_slurmctld(node_write_lock);

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(iter)))
		_set_assoc_list(resv_ptr);
	list_iterator_destroy(iter);

	unlock_slurmctld(node_write_lock);
}

extern void update_part_nodes_in_resv(part_record_t *part_ptr)
{
	list_itr_t *iter = NULL;
	slurmctld_resv_t *resv_ptr = NULL;
	xassert(part_ptr);

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(iter))) {
		if ((resv_ptr->flags & RESERVE_FLAG_PART_NODES) &&
		    (resv_ptr->partition != NULL) &&
		    (xstrcmp(resv_ptr->partition, part_ptr->name) == 0)) {
			slurmctld_resv_t old_resv_ptr;
			memset(&old_resv_ptr, 0, sizeof(slurmctld_resv_t));
			old_resv_ptr.assoc_list = resv_ptr->assoc_list;
			old_resv_ptr.flags = resv_ptr->flags;
			old_resv_ptr.node_list = resv_ptr->node_list;
			resv_ptr->node_list = NULL;
			FREE_NULL_BITMAP(resv_ptr->node_bitmap);
			resv_ptr->node_bitmap = bit_copy(part_ptr->node_bitmap);
			resv_ptr->node_cnt = bit_set_count(resv_ptr->
							   node_bitmap);
			resv_ptr->node_list = xstrdup(part_ptr->nodes);
			old_resv_ptr.tres_str = resv_ptr->tres_str;
			resv_ptr->tres_str = NULL;
			_set_tres_cnt(resv_ptr, &old_resv_ptr);
			old_resv_ptr.assoc_list = NULL;
			xfree(old_resv_ptr.tres_str);
			xfree(old_resv_ptr.node_list);
			last_resv_update = time(NULL);
			_set_boot_time(resv_ptr);
		}
	}
	list_iterator_destroy(iter);
}

extern bool job_borrow_from_resv_check(job_record_t *job_ptr,
				       job_record_t *preemptor_ptr)
{
	/*
	 * If this job is running in a reservation, but not belonging to the
	 * reservation directly.
	 */
	if (job_uses_max_start_delay_resv(preemptor_ptr) &&
	    (job_ptr->warn_flags & KILL_JOB_RESV) &&
	    job_ptr->node_bitmap &&
	    bit_overlap_any(job_ptr->node_bitmap,
			    preemptor_ptr->resv_ptr->node_bitmap))
		return true;
	return false;
}

extern bool job_uses_max_start_delay_resv(job_record_t *job_ptr)
{
	if (job_ptr->resv_ptr && job_ptr->resv_ptr->max_start_delay &&
	    job_ptr->resv_ptr->node_bitmap)
		return true;
	return false;
}
static void _set_nodes_flags(slurmctld_resv_t *resv_ptr, time_t now,
			     uint32_t flags, bool reset_all)
{
	node_record_t *node_ptr;
	uint32_t old_state;
	bitstr_t *maint_node_bitmap = NULL;
	slurmctld_resv_t *resv2_ptr;

	if (!resv_ptr->node_bitmap) {
		if ((resv_ptr->flags & RESERVE_FLAG_ANY_NODES) == 0) {
			error("%s: reservation %s lacks a bitmap",
			      __func__, resv_ptr->name);
		}
		return;
	}

	if (!bit_set_count(resv_ptr->node_bitmap)) {
		if ((resv_ptr->flags & RESERVE_FLAG_ANY_NODES) == 0) {
			error("%s: reservation %s includes no nodes",
			      __func__, resv_ptr->name);
		}
		return;
	}

	if (!(resv_ptr->ctld_flags & RESV_CTLD_NODE_FLAGS_SET) && !reset_all &&
	    (resv_ptr->flags & RESERVE_FLAG_MAINT)) {
		maint_node_bitmap = bit_alloc(node_record_count);
		list_itr_t *iter = list_iterator_create(resv_list);
		while ((resv2_ptr = list_next(iter))) {
			if (resv_ptr != resv2_ptr &&
			    resv2_ptr->ctld_flags & RESV_CTLD_NODE_FLAGS_SET &&
			    resv2_ptr->flags & RESERVE_FLAG_MAINT &&
			    resv2_ptr->node_bitmap) {
				bit_or(maint_node_bitmap,
				       resv2_ptr->node_bitmap);
			}
		}
		list_iterator_destroy(iter);
	}

	for (int i = 0;
	     (node_ptr = next_node_bitmap(resv_ptr->node_bitmap, &i)); i++) {
		old_state = node_ptr->node_state;
		if (resv_ptr->ctld_flags & RESV_CTLD_NODE_FLAGS_SET)
			node_ptr->node_state |= flags;
		else if (!maint_node_bitmap || !bit_test(maint_node_bitmap, i))
			node_ptr->node_state &= (~flags);
		/* mark that this node is now down if maint mode flag changed */
		bool state_change = ((old_state ^ node_ptr->node_state) &
				     NODE_STATE_MAINT) || reset_all;
		if (state_change && (IS_NODE_DOWN(node_ptr) ||
				    IS_NODE_DRAIN(node_ptr) ||
				    IS_NODE_FAIL(node_ptr))) {
			clusteracct_storage_g_node_down(
				acct_db_conn,
				node_ptr, now, NULL,
				slurm_conf.slurm_user_id);
		}
		xfree(node_ptr->resv_name);
		if (IS_NODE_RES(node_ptr))
			node_ptr->resv_name = xstrdup(resv_ptr->name);
	}
	FREE_NULL_BITMAP(maint_node_bitmap);
}

extern void job_resv_append_magnetic(job_queue_req_t *job_queue_req)
{
	if (!magnetic_resv_list || !list_count(magnetic_resv_list))
		return;

	list_for_each(magnetic_resv_list, _queue_magnetic_resv,
		      job_queue_req);
}

extern void job_resv_clear_magnetic_flag(job_record_t *job_ptr)
{
	if (!(job_ptr->bit_flags & JOB_MAGNETIC) ||
	    (job_ptr->job_state & JOB_RUNNING))
		return;

	xfree(job_ptr->resv_name);
	job_ptr->resv_id = 0;
	job_ptr->resv_ptr = NULL;
	job_ptr->bit_flags &= (~JOB_MAGNETIC);
}

extern bool validate_resv_uid(char *resv_name, uid_t uid)
{
	static time_t sched_update = 0;
	static bool user_resv_delete = false;

	slurmdb_assoc_rec_t assoc;
	list_t *assoc_list = NULL;
	assoc_mgr_lock_t locks = { .assoc = READ_LOCK };
	bool found_it = false;
	slurmctld_resv_t *resv_ptr;

	/* Make sure we have node write locks. */
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));

	if (!resv_name)
		return false;

	if (sched_update != slurm_conf.last_update) {
		if (xstrcasestr(slurm_conf.slurmctld_params,
		                "user_resv_delete"))
			user_resv_delete = true;
		else
			user_resv_delete = false;
		sched_update = slurm_conf.last_update;
	}

	if (!(resv_ptr = find_resv_name(resv_name)))
		return false;

	if ((!user_resv_delete) && !(resv_ptr->flags & RESERVE_FLAG_USER_DEL))
		return false;

	memset(&assoc, 0, sizeof(slurmdb_assoc_rec_t));
	assoc.uid = uid;

	assoc_list = list_create(NULL);

	assoc_mgr_lock(&locks);
	if (assoc_mgr_get_user_assocs(acct_db_conn, &assoc,
				      accounting_enforce, assoc_list)
	    != SLURM_SUCCESS)
		goto end_it;

	if (_validate_user_access(resv_ptr, assoc_list, uid))
		found_it = true;
end_it:
	FREE_NULL_LIST(assoc_list);
	assoc_mgr_unlock(&locks);

	return found_it;
}

/*
 * reservation_update_groups - reload the user_list of reservations
 *	with groups set
 * IN force - if set then always reload the user_list
 */
extern void reservation_update_groups(int force)
{
	static time_t last_update_time;
	int updated = 0;
	time_t temp_time;
	DEF_TIMERS;

	START_TIMER;
	temp_time = get_group_tlm();

	if (!force && (temp_time == last_update_time))
		return;

	debug2("Updating reservations group's uid access lists");

	last_update_time = temp_time;

	list_for_each(resv_list, _update_resv_group_uid_access_list, &updated);

	/*
	 * Only update last_resv_update when changes made
	 */
	if (updated) {
		debug2("%s: list updated, resetting last_resv_update time",
		       __func__);
		last_resv_update = time(NULL);
	}

	END_TIMER2(__func__);
}

/*
 * The following functions all deal with calculating the count of reserved
 * licenses for a given license. (Iterating across all licenses is handled
 * upstream of this function.)
 * This is done by iterating across all reservations, checking if the
 * reservation is currently active, and then if it matches the license to
 * update we add the reserved license count in. This is O(reservations *
 * licenses) which is not ideal, but the count of reservation and licenses on a
 * system tends to be low enough to ignore this overhead, and go with the
 * straightforward iterative solution presented here.
 */
static int _foreach_reservation_license(void *x, void *key)
{
	licenses_t *resv_license = (licenses_t *) x;
	licenses_t *license = (licenses_t *) key;

	if (!xstrcmp(resv_license->name, license->name))
		license->reserved += resv_license->total;

	return 0;
}

static int _foreach_reservation_license_list(void *x, void *key)
{
	slurmctld_resv_t *reservation = (slurmctld_resv_t *) x;
	time_t now = time(NULL);

	if (!reservation->license_list) {
		/* reservation without licenses */
		return 0;
	} else if (reservation->flags & RESERVE_FLAG_FLEX) {
		/*
		 * Treat FLEX reservations as always active
		 * and skip time bounds checks.
		 */
		;
	} else if (now < reservation->start_time) {
		/* reservation starts later */
		return 0;
	} else if (now > reservation->end_time) {
		/* reservation ended earlier */
		return 0;
	}

	list_for_each(reservation->license_list, _foreach_reservation_license,
		      key);

	return 0;
}

extern void set_reserved_license_count(licenses_t *license)
{
	license->reserved = 0;
	list_for_each(resv_list, _foreach_reservation_license_list,
		      license);
}

extern int get_magnetic_resv_count(void)
{
	xassert(magnetic_resv_list);

	return list_count(magnetic_resv_list);
}
