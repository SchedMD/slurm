/*****************************************************************************\
 *  reservation.c - resource reservation management
 *****************************************************************************
 *  Copyright (C) 2009-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2012-2014 SchedMD LLC
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif				/* WITH_PTHREADS */

#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"

#define _DEBUG		0
#define ONE_YEAR	(365 * 24 * 60 * 60)
#define RESV_MAGIC	0x3b82

/* Permit sufficient time for slurmctld failover or other long delay before
 * considering a reservation time specification being invalid */
#define MAX_RESV_DELAY	600

/* Change RESV_STATE_VERSION value when changing the state save format
 * Add logic to permit reading of the previous version's state in order
 * to avoid losing reservations between releases major SLURM updates. */
#define RESV_STATE_VERSION          "PROTOCOL_VERSION"
#define RESV_2_6_STATE_VERSION      "VER004"	/* SLURM version 2.6 */

typedef struct resv_thread_args {
	char *script;
	char *resv_name;
} resv_thread_args_t;

time_t    last_resv_update = (time_t) 0;
List      resv_list = (List) NULL;
uint32_t  resv_over_run;
uint32_t  top_suffix = 0;

#ifdef HAVE_BG
uint32_t  cpu_mult = 0;
uint32_t  cnodes_per_mp = 0;
uint32_t  cpus_per_mp = 0;
#endif

static void _advance_resv_time(slurmctld_resv_t *resv_ptr);
static void _advance_time(time_t *res_time, int day_cnt);
static int  _build_account_list(char *accounts, int *account_cnt,
				char ***account_list, bool *account_not);
static int  _build_uid_list(char *users, int *user_cnt, uid_t **user_list,
			    bool *user_not);
static void _clear_job_resv(slurmctld_resv_t *resv_ptr);
static slurmctld_resv_t *_copy_resv(slurmctld_resv_t *resv_orig_ptr);
static void _del_resv_rec(void *x);
static void _dump_resv_req(resv_desc_msg_t *resv_ptr, char *mode);
static int  _find_resv_id(void *x, void *key);
static int  _find_resv_name(void *x, void *key);
static void *_fork_script(void *x);
static void _free_script_arg(resv_thread_args_t *args);
static void _generate_resv_id(void);
static void _generate_resv_name(resv_desc_msg_t *resv_ptr);
static uint32_t _get_job_duration(struct job_record *job_ptr);
static bool _is_account_valid(char *account);
static bool _is_resv_used(slurmctld_resv_t *resv_ptr);
static bool _job_overlap(time_t start_time, uint32_t flags,
			 bitstr_t *node_bitmap);
static List _list_dup(List license_list);
static int  _open_resv_state_file(char **state_file);
static void _pack_resv(slurmctld_resv_t *resv_ptr, Buf buffer,
		       bool internal, uint16_t protocol_version);
static bitstr_t *_pick_idle_nodes(bitstr_t *avail_nodes,
				  resv_desc_msg_t *resv_desc_ptr,
				  bitstr_t **core_bitmap);
static bitstr_t *_pick_idle_node_cnt(bitstr_t *avail_bitmap,
				     resv_desc_msg_t *resv_desc_ptr,
				     uint32_t node_cnt,
				     bitstr_t **core_bitmap);
static int  _post_resv_create(slurmctld_resv_t *resv_ptr);
static int  _post_resv_delete(slurmctld_resv_t *resv_ptr);
static int  _post_resv_update(slurmctld_resv_t *resv_ptr,
			      slurmctld_resv_t *old_resv_ptr);
static int  _resize_resv(slurmctld_resv_t *resv_ptr, uint32_t node_cnt);
static void _restore_resv(slurmctld_resv_t *dest_resv,
			  slurmctld_resv_t *src_resv);
static bool _resv_overlap(time_t start_time, time_t end_time,
			  uint32_t flags, bitstr_t *node_bitmap,
			  slurmctld_resv_t *this_resv_ptr);
static void _run_script(char *script, slurmctld_resv_t *resv_ptr);
static int  _select_nodes(resv_desc_msg_t *resv_desc_ptr,
			  struct part_record **part_ptr,
			  bitstr_t **resv_bitmap, bitstr_t **core_bitmap);
static int  _set_assoc_list(slurmctld_resv_t *resv_ptr);
static void _set_cpu_cnt(slurmctld_resv_t *resv_ptr);
static void _set_nodes_flags(slurmctld_resv_t *resv_ptr, time_t now,
			     uint32_t flags);
static int  _update_account_list(slurmctld_resv_t *resv_ptr,
				 char *accounts);
static int  _update_uid_list(slurmctld_resv_t *resv_ptr, char *users);
static void _validate_all_reservations(void);
static int  _valid_job_access_resv(struct job_record *job_ptr,
				   slurmctld_resv_t *resv_ptr);
static bool _validate_one_reservation(slurmctld_resv_t *resv_ptr);
static void _validate_node_choice(slurmctld_resv_t *resv_ptr);

/* Advance res_time by the specified day count,
 * account for daylight savings time */
static void _advance_time(time_t *res_time, int day_cnt)
{
	time_t save_time = *res_time;
	struct tm time_tm;

	localtime_r(res_time, &time_tm);
	time_tm.tm_isdst = -1;
	time_tm.tm_mday += day_cnt;
	*res_time = mktime(&time_tm);
	if (*res_time == (time_t)(-1)) {
		error("Could not compute reservation time %lu",
		      (long unsigned int) save_time);
		*res_time = save_time + (24 * 60 * 60);
	}
}

static List _list_dup(List license_list)
{
	ListIterator iter;
	licenses_t *license_src, *license_dest;
	List lic_list = (List) NULL;

	if (!license_list)
		return lic_list;

	lic_list = list_create(license_free_rec);
	iter = list_iterator_create(license_list);
	while ((license_src = (licenses_t *) list_next(iter))) {
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
	resv_copy_ptr->account_cnt = resv_orig_ptr->account_cnt;
	resv_copy_ptr->account_list = xmalloc(sizeof(char *) *
					      resv_orig_ptr->account_cnt);
	resv_copy_ptr->account_not = resv_orig_ptr->account_not;
	for (i=0; i<resv_copy_ptr->account_cnt; i++) {
		resv_copy_ptr->account_list[i] =
				xstrdup(resv_orig_ptr->account_list[i]);
	}
	resv_copy_ptr->assoc_list = xstrdup(resv_orig_ptr->assoc_list);
	if (resv_orig_ptr->core_bitmap) {
		resv_copy_ptr->core_bitmap = bit_copy(resv_orig_ptr->
						      core_bitmap);
	}
	resv_copy_ptr->cpu_cnt = resv_orig_ptr->cpu_cnt;
	resv_copy_ptr->duration = resv_orig_ptr->duration;
	resv_copy_ptr->end_time = resv_orig_ptr->end_time;
	resv_copy_ptr->features = xstrdup(resv_orig_ptr->features);
	resv_copy_ptr->flags = resv_orig_ptr->flags;
	resv_copy_ptr->full_nodes = resv_orig_ptr->full_nodes;
	resv_copy_ptr->job_pend_cnt = resv_orig_ptr->job_pend_cnt;
	resv_copy_ptr->job_run_cnt = resv_orig_ptr->job_run_cnt;
	resv_copy_ptr->licenses = xstrdup(resv_orig_ptr->licenses);
	resv_copy_ptr->license_list = _list_dup(resv_orig_ptr->
						license_list);
	resv_copy_ptr->magic = resv_orig_ptr->magic;
	resv_copy_ptr->flags_set_node = resv_orig_ptr->flags_set_node;
	resv_copy_ptr->name = xstrdup(resv_orig_ptr->name);
	resv_copy_ptr->node_bitmap = bit_copy(resv_orig_ptr->node_bitmap);
	resv_copy_ptr->node_cnt = resv_orig_ptr->node_cnt;
	resv_copy_ptr->node_list = xstrdup(resv_orig_ptr->node_list);
	resv_copy_ptr->partition = xstrdup(resv_orig_ptr->partition);
	resv_copy_ptr->part_ptr = resv_orig_ptr->part_ptr;
	resv_copy_ptr->resv_id = resv_orig_ptr->resv_id;
	resv_copy_ptr->start_time = resv_orig_ptr->start_time;
	resv_copy_ptr->start_time_first = resv_orig_ptr->start_time_first;
	resv_copy_ptr->start_time_prev = resv_orig_ptr->start_time_prev;
	resv_copy_ptr->users = xstrdup(resv_orig_ptr->users);
	resv_copy_ptr->user_cnt = resv_orig_ptr->user_cnt;
	resv_copy_ptr->user_list = xmalloc(sizeof(uid_t) *
					   resv_orig_ptr->user_cnt);
	resv_copy_ptr->user_not = resv_orig_ptr->user_not;
	for (i=0; i<resv_copy_ptr->user_cnt; i++)
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

	dest_resv->account_not = src_resv->account_not;

	xfree(dest_resv->assoc_list);
	dest_resv->assoc_list = src_resv->assoc_list;
	src_resv->assoc_list = NULL;

	FREE_NULL_BITMAP(dest_resv->core_bitmap);
	dest_resv->core_bitmap = src_resv->core_bitmap;
	src_resv->core_bitmap = NULL;

	dest_resv->cpu_cnt = src_resv->cpu_cnt;
	dest_resv->duration = src_resv->duration;
	dest_resv->end_time = src_resv->end_time;

	xfree(dest_resv->features);
	dest_resv->features = src_resv->features;
	src_resv->features = NULL;

	dest_resv->flags = src_resv->flags;
	dest_resv->full_nodes = src_resv->full_nodes;
	dest_resv->job_pend_cnt = src_resv->job_pend_cnt;
	dest_resv->job_run_cnt = src_resv->job_run_cnt;

	xfree(dest_resv->licenses);
	dest_resv->licenses = src_resv->licenses;
	src_resv->licenses = NULL;

	if (dest_resv->license_list)
		list_destroy(dest_resv->license_list);
	dest_resv->license_list = src_resv->license_list;
	src_resv->license_list = NULL;

	dest_resv->magic = src_resv->magic;
	dest_resv->flags_set_node = src_resv->flags_set_node;

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
		xassert(resv_ptr->magic == RESV_MAGIC);
		resv_ptr->magic = 0;
		xfree(resv_ptr->accounts);
		for (i=0; i<resv_ptr->account_cnt; i++)
			xfree(resv_ptr->account_list[i]);
		xfree(resv_ptr->account_list);
		xfree(resv_ptr->assoc_list);
		FREE_NULL_BITMAP(resv_ptr->core_bitmap);
		xfree(resv_ptr->features);
		if (resv_ptr->license_list)
			list_destroy(resv_ptr->license_list);
		xfree(resv_ptr->licenses);
		xfree(resv_ptr->name);
		FREE_NULL_BITMAP(resv_ptr->node_bitmap);
		xfree(resv_ptr->node_list);
		xfree(resv_ptr->partition);
		xfree(resv_ptr->users);
		xfree(resv_ptr->user_list);
		xfree(resv_ptr);
	}
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

static int _find_resv_name(void *x, void *key)
{
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) x;

	xassert(resv_ptr->magic == RESV_MAGIC);

	if (strcmp(resv_ptr->name, (char *) key))
		return 0;
	else
		return 1;	/* match */
}

static void _dump_resv_req(resv_desc_msg_t *resv_ptr, char *mode)
{

	char start_str[32] = "-1", end_str[32] = "-1", *flag_str = NULL;
	char *node_cnt_str = NULL;
	int duration, i;

	if (!(slurmctld_conf.debug_flags & DEBUG_FLAG_RESERVATION))
		return;

	if (resv_ptr->start_time != (time_t) NO_VAL) {
		slurm_make_time_str(&resv_ptr->start_time,
				    start_str, sizeof(start_str));
	}
	if (resv_ptr->end_time != (time_t) NO_VAL) {
		slurm_make_time_str(&resv_ptr->end_time,
				    end_str,  sizeof(end_str));
	}
	if (resv_ptr->flags != NO_VAL)
		flag_str = reservation_flags_string(resv_ptr->flags);

	if (resv_ptr->duration == NO_VAL)
		duration = -1;
	else
		duration = resv_ptr->duration;

	if (resv_ptr->node_cnt) {
		for (i = 0; resv_ptr->node_cnt[i]; i++) {
			if (node_cnt_str) {
				xstrfmtcat(node_cnt_str, ",%u",
					   resv_ptr->node_cnt[i]);
			} else {
				xstrfmtcat(node_cnt_str, "%u",
					   resv_ptr->node_cnt[i]);
			}
		}
	}

	info("%s: Name=%s StartTime=%s EndTime=%s Duration=%d "
	     "Flags=%s NodeCnt=%s NodeList=%s Features=%s "
	     "PartitionName=%s Users=%s Accounts=%s Licenses=%s",
	     mode, resv_ptr->name, start_str, end_str, duration,
	     flag_str, node_cnt_str, resv_ptr->node_list,
	     resv_ptr->features, resv_ptr->partition,
	     resv_ptr->users, resv_ptr->accounts, resv_ptr->licenses);

	xfree(flag_str);
	xfree(node_cnt_str);
}

static void _generate_resv_id(void)
{
	while (1) {
		if (top_suffix >= 9999)
			top_suffix = 1;		/* wrap around */
		else
			top_suffix++;
		if (!list_find_first(resv_list, _find_resv_id, &top_suffix))
			break;
	}
}

static void _generate_resv_name(resv_desc_msg_t *resv_ptr)
{
	char *key, *name, *sep;
	int len;

	/* Generate name prefix, based upon the first account
	 * name if provided otherwise first user name */
	if (resv_ptr->accounts && resv_ptr->accounts[0])
		key = resv_ptr->accounts;
	else
		key = resv_ptr->users;
	if (key[0] == '-')
		key++;
	sep = strchr(key, ',');
	if (sep)
		len = sep - key;
	else
		len = strlen(key);
	name = xmalloc(len + 16);
	strncpy(name, key, len);

	xstrfmtcat(name, "_%d", top_suffix);
	len++;

	resv_ptr->name = name;
}

/* Validate an account name */
static bool _is_account_valid(char *account)
{
	slurmdb_association_rec_t assoc_rec, *assoc_ptr;

	if (!(accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS))
		return true;	/* don't worry about account validity */

	memset(&assoc_rec, 0, sizeof(slurmdb_association_rec_t));
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
static int _append_assoc_list(List assoc_list, slurmdb_association_rec_t *assoc)
{
	int rc = ESLURM_INVALID_ACCOUNT;
	slurmdb_association_rec_t *assoc_ptr = NULL;
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
	List assoc_list_allow = NULL, assoc_list_deny = NULL, assoc_list;
	slurmdb_association_rec_t assoc, *assoc_ptr = NULL;
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };


	/* no need to do this if we can't ;) */
	if (!association_based_accounting)
		return rc;

	assoc_list_allow = list_create(NULL);
	assoc_list_deny  = list_create(NULL);

	memset(&assoc, 0, sizeof(slurmdb_association_rec_t));
	xfree(resv_ptr->assoc_list);

	assoc_mgr_lock(&locks);
	if (resv_ptr->account_cnt && resv_ptr->user_cnt) {
		if (!resv_ptr->account_not && !resv_ptr->user_not) {
			/* Add every association that matches both account
			 * and user */
			for (i=0; i < resv_ptr->user_cnt; i++) {
				for (j=0; j < resv_ptr->account_cnt; j++) {
					memset(&assoc, 0,
					       sizeof(slurmdb_association_rec_t));
					assoc.acct = resv_ptr->account_list[j];
					assoc.uid  = resv_ptr->user_list[i];
					rc = _append_assoc_list(
						assoc_list_allow, &assoc);
					if (rc != SLURM_SUCCESS)
						goto end_it;
				}
			}
		} else {
			if (resv_ptr->user_not)
				assoc_list = assoc_list_deny;
			else
				assoc_list = assoc_list_allow;
			for (i=0; i < resv_ptr->user_cnt; i++) {
				memset(&assoc, 0,
				       sizeof(slurmdb_association_rec_t));
				assoc.uid = resv_ptr->user_list[i];
				rc = assoc_mgr_get_user_assocs(
					    acct_db_conn, &assoc,
					    accounting_enforce,
					    assoc_list);
				if (rc != SLURM_SUCCESS) {
					error("No associations for UID %u",
					      assoc.uid);
					rc = ESLURM_INVALID_ACCOUNT;
					goto end_it;
				}
			}
			if (resv_ptr->account_not)
				assoc_list = assoc_list_deny;
			else
				assoc_list = assoc_list_allow;
			for (j=0; j < resv_ptr->account_cnt; j++) {
				memset(&assoc, 0,
				       sizeof(slurmdb_association_rec_t));
				assoc.acct = resv_ptr->account_list[j];
				assoc.uid  = (uint32_t)NO_VAL;
				rc = _append_assoc_list(assoc_list, &assoc);
				if (rc != SLURM_SUCCESS)
					goto end_it;
			}
		}
	} else if (resv_ptr->user_cnt) {
		if (resv_ptr->user_not)
			assoc_list = assoc_list_deny;
		else
			assoc_list = assoc_list_allow;
		for (i=0; i < resv_ptr->user_cnt; i++) {
			memset(&assoc, 0, sizeof(slurmdb_association_rec_t));
			assoc.uid = resv_ptr->user_list[i];
			rc = assoc_mgr_get_user_assocs(
				    acct_db_conn, &assoc,
				    accounting_enforce, assoc_list);
			if (rc != SLURM_SUCCESS) {
				error("No associations for UID %u",
				      assoc.uid);
				rc = ESLURM_INVALID_ACCOUNT;
				goto end_it;
			}
		}
	} else if (resv_ptr->account_cnt) {
		if (resv_ptr->account_not)
			assoc_list = assoc_list_deny;
		else
			assoc_list = assoc_list_allow;
		for (i=0; i < resv_ptr->account_cnt; i++) {
			memset(&assoc, 0, sizeof(slurmdb_association_rec_t));
			assoc.acct = resv_ptr->account_list[i];
			assoc.uid  = (uint32_t)NO_VAL;
			if ((rc = _append_assoc_list(assoc_list, &assoc))
			    != SLURM_SUCCESS) {
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
		ListIterator itr = list_iterator_create(assoc_list_allow);
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
		ListIterator itr = list_iterator_create(assoc_list_deny);
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
	list_destroy(assoc_list_allow);
	list_destroy(assoc_list_deny);
	assoc_mgr_unlock(&locks);

	return rc;
}

/* Post reservation create */
static int _post_resv_create(slurmctld_resv_t *resv_ptr)
{
	int rc = SLURM_SUCCESS;
	slurmdb_reservation_rec_t resv;
	char temp_bit[BUF_SIZE];

	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT)
		return rc;

	memset(&resv, 0, sizeof(slurmdb_reservation_rec_t));
	resv.assocs = resv_ptr->assoc_list;
	resv.cluster = slurmctld_cluster_name;
	resv.cpus = resv_ptr->cpu_cnt;
#ifdef HAVE_BG
	/* Since on a bluegene we track cnodes instead of cpus do the
	   adjustment since accounting is expecting cpus here.
	*/
	if (!cpu_mult)
		(void)select_g_alter_node_cnt(
			SELECT_GET_NODE_CPU_CNT, &cpu_mult);
	resv.cpus *= cpu_mult;
#endif
	resv.flags = resv_ptr->flags;
	resv.id = resv_ptr->resv_id;
	resv.name = resv_ptr->name;
	resv.nodes = resv_ptr->node_list;
	if (resv_ptr->node_bitmap) {
		resv.node_inx = bit_fmt(temp_bit, sizeof(temp_bit),
					resv_ptr->node_bitmap);
	}
	resv.time_end = resv_ptr->end_time;
	resv.time_start = resv_ptr->start_time;

	rc = acct_storage_g_add_reservation(acct_db_conn, &resv);

	return rc;
}

/* Note that a reservation has been deleted */
static int _post_resv_delete(slurmctld_resv_t *resv_ptr)
{
	int rc = SLURM_SUCCESS;
	slurmdb_reservation_rec_t resv;

	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT)
		return rc;

	memset(&resv, 0, sizeof(slurmdb_reservation_rec_t));
	resv.cluster = slurmctld_cluster_name;
	resv.id = resv_ptr->resv_id;
	resv.name = resv_ptr->name;
	resv.time_start = resv_ptr->start_time;
	/* This is just a time stamp here to delete if the reservation
	 * hasn't started yet so we don't get trash records in the
	 * database if said database isn't up right now */
	resv.time_start_prev = time(NULL);
	rc = acct_storage_g_remove_reservation(acct_db_conn, &resv);

	return rc;
}

/* Note that a reservation has been updated */
static int _post_resv_update(slurmctld_resv_t *resv_ptr,
			     slurmctld_resv_t *old_resv_ptr)
{
	int rc = SLURM_SUCCESS;
	slurmdb_reservation_rec_t resv;
	char temp_bit[BUF_SIZE];

	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT)
		return rc;

	memset(&resv, 0, sizeof(slurmdb_reservation_rec_t));
	resv.cluster = slurmctld_cluster_name;
	resv.id = resv_ptr->resv_id;
	resv.time_end = resv_ptr->end_time;

	if (!old_resv_ptr) {
		resv.assocs = resv_ptr->assoc_list;
		resv.cpus = resv_ptr->cpu_cnt;
#ifdef HAVE_BG
		/* Since on a bluegene we track cnodes instead of cpus
		 * do the adjustment since accounting is expecting
		 * cpus here.
		 */
		if (!cpu_mult)
			(void)select_g_alter_node_cnt(
				SELECT_GET_NODE_CPU_CNT, &cpu_mult);
		resv.cpus *= cpu_mult;
#endif
		resv.flags = resv_ptr->flags;
		resv.nodes = resv_ptr->node_list;
	} else {
		time_t now = time(NULL);

		if (old_resv_ptr->assoc_list && resv_ptr->assoc_list) {
			if (strcmp(old_resv_ptr->assoc_list,
				  resv_ptr->assoc_list))
				resv.assocs = resv_ptr->assoc_list;
		} else if (resv_ptr->assoc_list)
			resv.assocs = resv_ptr->assoc_list;

		if (old_resv_ptr->cpu_cnt != resv_ptr->cpu_cnt) {
			resv.cpus = resv_ptr->cpu_cnt;
#ifdef HAVE_BG
			/* Since on a bluegene we track cnodes instead
			 * of cpus do the adjustment since accounting
			 * is expecting cpus here.
			 */
			if (!cpu_mult)
				(void)select_g_alter_node_cnt(
					SELECT_GET_NODE_CPU_CNT, &cpu_mult);
			resv.cpus *= cpu_mult;
#endif
		} else
			resv.cpus = (uint32_t)NO_VAL;

		if (old_resv_ptr->flags != resv_ptr->flags)
			resv.flags = resv_ptr->flags;
		else
			resv.flags = NO_VAL;

		if (old_resv_ptr->node_list && resv_ptr->node_list) {
			if (strcmp(old_resv_ptr->node_list,
				  resv_ptr->node_list))
				resv.nodes = resv_ptr->node_list;
		} else if (resv_ptr->node_list)
			resv.nodes = resv_ptr->node_list;

		/* Here if the reservation has started already we need
		 * to mark a new start time for it if certain
		 * variables are needed in accounting.  Right now if
		 * the assocs, nodes, flags or cpu count changes we need a
		 * new start time of now. */
		if ((resv_ptr->start_time < now)
		     && (resv.assocs
			 || resv.nodes
			 || (resv.flags != NO_VAL)
			 || (resv.cpus != NO_VAL))) {
			resv_ptr->start_time_prev = resv_ptr->start_time;
			resv_ptr->start_time = now;
		}
	}
	/* now set the (maybe new) start_times */
	resv.time_start = resv_ptr->start_time;
	resv.time_start_prev = resv_ptr->start_time_prev;

	if (resv.nodes && resv_ptr->node_bitmap) {
		resv.node_inx = bit_fmt(temp_bit, sizeof(temp_bit),
					resv_ptr->node_bitmap);
	}

	rc = acct_storage_g_modify_reservation(acct_db_conn, &resv);

	return rc;
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
	ac_list = xmalloc(sizeof(char *) * (i + 2));
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
	int ac_cnt = 0, i, j, k;
	int *ac_type, minus_account = 0, plus_account = 0;
	char **ac_list;
	bool found_it;
	bool account_not = false;

	if (!accounts)
		return ESLURM_INVALID_ACCOUNT;

	i = strlen(accounts);
	ac_list = xmalloc(sizeof(char *) * (i + 2));
	ac_type = xmalloc(sizeof(int)    * (i + 2));
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
		xfree(resv_ptr->account_list);
		resv_ptr->account_list = ac_list;
		resv_ptr->account_cnt  = ac_cnt;
		resv_ptr->account_not  = account_not;
		xfree(ac_cpy);
		xfree(ac_type);
		return SLURM_SUCCESS;
	}

	/* Modification of existing account list */
	if ((resv_ptr->account_cnt == 0) && minus_account)
		resv_ptr->account_not = true;
	if (resv_ptr->account_not) {
		/* change minus_account to plus_account (add to NOT list) and
		 * change plus_account to minus_account (remove from NOT list) */
		for (i = 0; i < ac_cnt; i++) {
			if (ac_type[i] == 1)
				ac_type[i] = 2;
			else if (ac_type[i] == 2)
				ac_type[i] = 1;
		}
		if (minus_account && !plus_account) {
			minus_account = false;
			plus_account  = true;
		} else if (!minus_account && plus_account) {
			minus_account = true;
			plus_account  = false;
		}
	}
	if (minus_account) {
		if (resv_ptr->account_cnt == 0)
			goto inval;
		for (i=0; i<ac_cnt; i++) {
			if (ac_type[i] != 1)	/* not minus */
				continue;
			found_it = false;
			for (j=0; j<resv_ptr->account_cnt; j++) {
				char *test_name = resv_ptr->account_list[j];
				if (test_name[0] == '-')
					test_name++;
				if (strcmp(test_name, ac_list[i]))
					continue;
				found_it = true;
				xfree(resv_ptr->account_list[j]);
				resv_ptr->account_cnt--;
				for (k=j; k<resv_ptr->account_cnt; k++) {
					resv_ptr->account_list[k] =
						resv_ptr->account_list[k+1];
				}
				break;
			}
			if (!found_it)
				goto inval;
		}
		xfree(resv_ptr->accounts);
		for (i=0; i<resv_ptr->account_cnt; i++) {
			if (i)
				xstrcat(resv_ptr->accounts, ",");
			if (resv_ptr->account_not)
				xstrcat(resv_ptr->accounts, "-");
			xstrcat(resv_ptr->accounts, resv_ptr->account_list[i]);
		}
	}

	if (plus_account) {
		for (i=0; i<ac_cnt; i++) {
			if (ac_type[i] != 2)	/* not plus */
				continue;
			found_it = false;
			for (j=0; j<resv_ptr->account_cnt; j++) {
				char *test_name = resv_ptr->account_list[j];
				if (test_name[0] == '-')
					test_name++;
				if (strcmp(test_name, ac_list[i]))
					continue;
				found_it = true;
				break;
			}
			if (found_it)
				continue;	/* duplicate entry */
			xrealloc(resv_ptr->account_list,
				 sizeof(char *) * (resv_ptr->account_cnt + 1));
			resv_ptr->account_list[resv_ptr->account_cnt++] =
					xstrdup(ac_list[i]);
		}
		xfree(resv_ptr->accounts);
		for (i=0; i<resv_ptr->account_cnt; i++) {
			if (i)
				xstrcat(resv_ptr->accounts, ",");
			if (resv_ptr->account_not)
				xstrcat(resv_ptr->accounts, "-");
			xstrcat(resv_ptr->accounts, resv_ptr->account_list[i]);
		}
	}

	for (i=0; i<ac_cnt; i++)
		xfree(ac_list[i]);
	xfree(ac_list);
	xfree(ac_type);
	xfree(ac_cpy);
	return SLURM_SUCCESS;

 inval:	for (i=0; i<ac_cnt; i++)
		xfree(ac_list[i]);
	xfree(ac_list);
	xfree(ac_type);
	xfree(ac_cpy);
	return ESLURM_INVALID_ACCOUNT;
}

/*
 * Validate a comma delimited list of user names and build an array of
 *	their UIDs
 * IN users      - a list of user names
 * OUT user_cnt  - number of UIDs in the list
 * OUT user_list - list of the user's uid, CALLER MUST XFREE;
 * OUT user_not  - true of user_list is that of users to be blocked
 *                 from reservation access
 * RETURN 0 on success
 */
static int _build_uid_list(char *users, int *user_cnt, uid_t **user_list,
			   bool *user_not)
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
	u_list = xmalloc(sizeof(uid_t) * (i + 2));
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
			goto inval;
		}
		u_list[u_cnt++] = u_tmp;
		tok = strtok_r(NULL, ",", &last);
	}
	*user_cnt  = u_cnt;
	*user_list = u_list;
	xfree(tmp);
	return SLURM_SUCCESS;

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
	char *last = NULL, *u_cpy = NULL, *tmp = NULL, *tok;
	int u_cnt = 0, i, j, k;
	uid_t *u_list, u_tmp;
	int *u_type, minus_user = 0, plus_user = 0;
	char **u_name;
	bool found_it;
	bool user_not = false;

	if (!users)
		return ESLURM_USER_ID_MISSING;

	/* Parse the incoming user expression */
	i = strlen(users);
	u_list = xmalloc(sizeof(uid_t)  * (i + 2));
	u_name = xmalloc(sizeof(char *) * (i + 2));
	u_type = xmalloc(sizeof(int)    * (i + 2));
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
		resv_ptr->user_not  = user_not;
		xfree(u_cpy);
		xfree(u_name);
		xfree(u_type);
		return SLURM_SUCCESS;
	}

	/* Modification of existing user list */
	if ((resv_ptr->user_cnt == 0) && minus_user)
		resv_ptr->user_not = true;
	if (resv_ptr->user_not) {
		/* change minus_user to plus_user (add to NOT list) and
		 * change plus_user to minus_user (remove from NOT list) */
		for (i = 0; i < u_cnt; i++) {
			if (u_type[i] == 1)
				u_type[i] = 2;
			else if (u_type[i] == 2)
				u_type[i] = 1;
		}
		if (minus_user && !plus_user) {
			minus_user = false;
			plus_user  = true;
		} else if (!minus_user && plus_user) {
			minus_user = true;
			plus_user  = false;
		}
	}
	if (minus_user) {
		for (i=0; i<u_cnt; i++) {
			if (u_type[i] != 1)	/* not minus */
				continue;
			found_it = false;
			for (j=0; j<resv_ptr->user_cnt; j++) {
				if (resv_ptr->user_list[j] != u_list[i])
					continue;
				found_it = true;
				resv_ptr->user_cnt--;
				for (k=j; k<resv_ptr->user_cnt; k++) {
					resv_ptr->user_list[k] =
						resv_ptr->user_list[k+1];
				}
				break;
			}
			if (!found_it)
				goto inval;

			/* Now we need to remove from users string */
			k = strlen(u_name[i]);
			tmp = resv_ptr->users;
			while ((tok = strstr(tmp, u_name[i]))) {
				if (((tok != resv_ptr->users) &&
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
				for (j=0; ; j++) {
					tok[j] = tok[j+k];
					if (tok[j] == '\0')
						break;
				}
			}
		}
		if ((resv_ptr->users == NULL) ||
		    (strlen(resv_ptr->users) == 0)) {
			resv_ptr->user_not = 0;
			xfree(resv_ptr->users);
		}
	}

	if (plus_user) {
		for (i=0; i<u_cnt; i++) {
			if (u_type[i] != 2)	/* not plus */
				continue;
			found_it = false;
			for (j=0; j<resv_ptr->user_cnt; j++) {
				if (resv_ptr->user_list[j] != u_list[i])
					continue;
				found_it = true;
				break;
			}
			if (found_it)
				continue;	/* duplicate entry */
			if (resv_ptr->users && resv_ptr->users[0])
				xstrcat(resv_ptr->users, ",");
			if (resv_ptr->user_not)
				xstrcat(resv_ptr->users, "-");
			xstrcat(resv_ptr->users, u_name[i]);
			xrealloc(resv_ptr->user_list,
				 sizeof(uid_t) * (resv_ptr->user_cnt + 1));
			resv_ptr->user_list[resv_ptr->user_cnt++] =
				u_list[i];
		}
	}
	xfree(u_cpy);
	xfree(u_list);
	xfree(u_name);
	xfree(u_type);
	return SLURM_SUCCESS;

 inval:	xfree(u_cpy);
	xfree(u_list);
	xfree(u_name);
	xfree(u_type);
	return ESLURM_USER_ID_MISSING;
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
static void _pack_resv(slurmctld_resv_t *resv_ptr, Buf buffer,
		       bool internal, uint16_t protocol_version)
{
	time_t now = time(NULL), start_relative, end_relative;

	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT)
		last_resv_update = now;
	if (!internal && (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT)) {
		start_relative = resv_ptr->start_time + now;
		if (resv_ptr->duration == INFINITE)
			end_relative = start_relative + ONE_YEAR;
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

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		packstr(resv_ptr->accounts,	buffer);
		pack32(resv_ptr->cpu_cnt,	buffer);
		pack_time(end_relative,		buffer);
		packstr(resv_ptr->features,	buffer);
		pack32(resv_ptr->flags,		buffer);
		packstr(resv_ptr->licenses,	buffer);
		packstr(resv_ptr->name,		buffer);
		pack32(resv_ptr->node_cnt,	buffer);
		packstr(resv_ptr->node_list,	buffer);
		packstr(resv_ptr->partition,	buffer);
		pack_time(start_relative,	buffer);
		packstr(resv_ptr->users,	buffer);

		if (internal) {
			uint32_t core_cnt = 0;
			pack8(resv_ptr->account_not,	buffer);
			packstr(resv_ptr->assoc_list,	buffer);
			if (resv_ptr->core_bitmap)
				core_cnt = bit_size(resv_ptr->core_bitmap);
			pack32(core_cnt,		buffer);
			pack_bit_fmt(resv_ptr->core_bitmap, buffer);
			pack32(resv_ptr->duration,	buffer);
			pack8(resv_ptr->full_nodes,	buffer);
			pack32(resv_ptr->resv_id,	buffer);
			pack_time(resv_ptr->start_time_prev,	buffer);
			pack_time(resv_ptr->start_time,	buffer);
			pack8(resv_ptr->user_not,	buffer);
		} else {
			pack_bit_fmt(resv_ptr->node_bitmap, buffer);
		}
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		uint16_t flags;
		packstr(resv_ptr->accounts,	buffer);
		pack32(resv_ptr->cpu_cnt,	buffer);
		pack_time(resv_ptr->end_time,	buffer);
		packstr(resv_ptr->features,	buffer);
		flags = resv_ptr->flags;
		pack16(flags,			buffer);
		packstr(resv_ptr->licenses,	buffer);
		packstr(resv_ptr->name,		buffer);
		pack32(resv_ptr->node_cnt,	buffer);
		packstr(resv_ptr->node_list,	buffer);
		packstr(resv_ptr->partition,	buffer);
		pack_time(resv_ptr->start_time_first,	buffer);
		packstr(resv_ptr->users,	buffer);

		if (internal) {
			uint32_t core_cnt = 0;
			pack8(resv_ptr->account_not,	buffer);
			packstr(resv_ptr->assoc_list,	buffer);
			if (resv_ptr->core_bitmap)
				core_cnt = bit_size(resv_ptr->core_bitmap);
			pack32(core_cnt,		buffer);
			pack_bit_fmt(resv_ptr->core_bitmap, buffer);
			pack32(resv_ptr->duration,	buffer);
			pack8(resv_ptr->full_nodes,	buffer);
			pack32(resv_ptr->resv_id,	buffer);
			pack_time(resv_ptr->start_time_prev,	buffer);
			pack_time(resv_ptr->start_time,	buffer);
			pack8(resv_ptr->user_not,	buffer);
		} else {
			pack_bit_fmt(resv_ptr->node_bitmap, buffer);
		}
	}
}

slurmctld_resv_t *_load_reservation_state(Buf buffer,
					  uint16_t protocol_version)
{
	slurmctld_resv_t *resv_ptr;
	uint32_t core_cnt, uint32_tmp;
	char *core_inx_str = NULL;

	resv_ptr = xmalloc(sizeof(slurmctld_resv_t));
	xassert(resv_ptr->magic = RESV_MAGIC);	/* Sets value */
	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&resv_ptr->accounts,
				       &uint32_tmp,	buffer);
		safe_unpack32(&resv_ptr->cpu_cnt,	buffer);
		safe_unpack_time(&resv_ptr->end_time,	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->features,
				       &uint32_tmp, 	buffer);
		safe_unpack32(&resv_ptr->flags,		buffer);
		safe_unpackstr_xmalloc(&resv_ptr->licenses,
				       &uint32_tmp, 	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->name,	&uint32_tmp, buffer);

		safe_unpack32(&resv_ptr->node_cnt,	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->node_list,
				       &uint32_tmp,	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->partition,
				       &uint32_tmp, 	buffer);
		safe_unpack_time(&resv_ptr->start_time_first,	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->users, &uint32_tmp, buffer);

		/* Fields saved for internal use only (save state) */
		safe_unpack8((uint8_t *)&resv_ptr->account_not,	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->assoc_list,
				       &uint32_tmp,	buffer);
		safe_unpack32(&core_cnt,		buffer);
		safe_unpackstr_xmalloc(&core_inx_str, &uint32_tmp, buffer);
		if (core_inx_str == NULL) {
#if _DEBUG
			info("Reservation %s has no core_bitmap",
			     resv_ptr->name);
#endif
		} else {
			resv_ptr->core_bitmap = bit_alloc(core_cnt);
			bit_unfmt(resv_ptr->core_bitmap, core_inx_str);
			info("Reservation %s has core_bitmap %s on node %s",
			     resv_ptr->name, core_inx_str,
			     resv_ptr->node_list);
			xfree(core_inx_str);
		}
		safe_unpack32(&resv_ptr->duration,	buffer);
		safe_unpack8((uint8_t *)&resv_ptr->full_nodes,	buffer);
		safe_unpack32(&resv_ptr->resv_id,	buffer);
		safe_unpack_time(&resv_ptr->start_time_prev, buffer);
		safe_unpack_time(&resv_ptr->start_time, buffer);
		safe_unpack8((uint8_t *)&resv_ptr->user_not,	buffer);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		uint16_t flags;
		safe_unpackstr_xmalloc(&resv_ptr->accounts,
				       &uint32_tmp,	buffer);
		safe_unpack32(&resv_ptr->cpu_cnt,	buffer);
		safe_unpack_time(&resv_ptr->end_time,	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->features,
				       &uint32_tmp, 	buffer);
		flags = resv_ptr->flags;
		safe_unpack16(&flags,			buffer);
		safe_unpackstr_xmalloc(&resv_ptr->licenses,
				       &uint32_tmp, 	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->name,	&uint32_tmp, buffer);

		safe_unpack32(&resv_ptr->node_cnt,	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->node_list,
				       &uint32_tmp,	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->partition,
				       &uint32_tmp, 	buffer);
		safe_unpack_time(&resv_ptr->start_time_first,	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->users, &uint32_tmp, buffer);

		/* Fields saved for internal use only (save state) */
		safe_unpack8((uint8_t *)&resv_ptr->account_not,	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->assoc_list,
				       &uint32_tmp,	buffer);
		safe_unpack32(&core_cnt,		buffer);
		safe_unpackstr_xmalloc(&core_inx_str, &uint32_tmp, buffer);
		if (core_inx_str == NULL) {
#if _DEBUG
			info("Reservation %s has no core_bitmap",
			     resv_ptr->name);
#endif
		} else {
			resv_ptr->core_bitmap = bit_alloc(core_cnt);
			bit_unfmt(resv_ptr->core_bitmap, core_inx_str);
			info("Reservation %s has core_bitmap %s on node %s",
			     resv_ptr->name, core_inx_str,
			     resv_ptr->node_list);
			xfree(core_inx_str);
		}
		safe_unpack32(&resv_ptr->duration,	buffer);
		safe_unpack8((uint8_t *)&resv_ptr->full_nodes,	buffer);
		safe_unpack32(&resv_ptr->resv_id,	buffer);
		safe_unpack_time(&resv_ptr->start_time_prev, buffer);
		safe_unpack_time(&resv_ptr->start_time, buffer);
		safe_unpack8((uint8_t *)&resv_ptr->user_not,	buffer);
	}
	return resv_ptr;

unpack_error:
	error("Incomplete reservation state save file");
	_del_resv_rec(resv_ptr);
	return NULL;
}

/*
 * Test if a new/updated reservation request will overlap running jobs
 * RET true if overlap
 */
static bool _job_overlap(time_t start_time, uint32_t flags,
			 bitstr_t *node_bitmap)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	bool overlap = false;

	if (flags & RESERVE_FLAG_IGN_JOBS)	/* ignore job overlap */
		return overlap;
	if (flags & RESERVE_FLAG_TIME_FLOAT)
		start_time += time(NULL);

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (IS_JOB_RUNNING(job_ptr)		&&
		    (job_ptr->end_time > start_time)	&&
		    (bit_overlap(job_ptr->node_bitmap, node_bitmap) > 0)) {
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
static bool _resv_overlap(time_t start_time, time_t end_time,
			  uint32_t flags, bitstr_t *node_bitmap,
			  slurmctld_resv_t *this_resv_ptr)
{
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;
	bool rc = false;
	int i, j;
	time_t s_time1, s_time2, e_time1, e_time2;

	if ((flags & RESERVE_FLAG_MAINT)   ||
	    (flags & RESERVE_FLAG_OVERLAP) ||
	    (!node_bitmap))
		return rc;

	iter = list_iterator_create(resv_list);

	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if (resv_ptr == this_resv_ptr)
			continue;	/* skip self */
		if (resv_ptr->node_bitmap == NULL)
			continue;	/* no specific nodes in reservation */
		if (!bit_overlap(resv_ptr->node_bitmap, node_bitmap))
			continue;	/* no overlap */
		if (!resv_ptr->full_nodes)
			continue;

		for (i=0; ((i<7) && (!rc)); i++) {  /* look forward one week */
			s_time1 = start_time;
			e_time1 = end_time;
			_advance_time(&s_time1, i);
			_advance_time(&e_time1, i);
			for (j=0; ((j<7) && (!rc)); j++) {
				s_time2 = resv_ptr->start_time;
				e_time2 = resv_ptr->end_time;
				_advance_time(&s_time2, j);
				_advance_time(&e_time2, j);
				if ((s_time1 < e_time2) &&
				    (e_time1 > s_time2)) {
					verbose("Reservation overlap with %s",
						resv_ptr->name);
					rc = true;
					break;
				}
				if (!(resv_ptr->flags & RESERVE_FLAG_DAILY))
					break;
			}
			if ((flags & RESERVE_FLAG_DAILY) == 0)
				break;
		}
	}
	list_iterator_destroy(iter);

	return rc;
}

/* Set a reservation's CPU count. Requires that the reservation's
 *	node_bitmap be set. */
static void _set_cpu_cnt(slurmctld_resv_t *resv_ptr)
{
	int i;
	uint32_t cpu_cnt = 0;
	struct node_record *node_ptr = node_record_table_ptr;

	if (!resv_ptr->node_bitmap)
		return;

#ifdef HAVE_BG
	if (!cnodes_per_mp)
		select_g_alter_node_cnt(SELECT_GET_NODE_SCALING,
					&cnodes_per_mp);
#endif

	for (i=0; i<node_record_count; i++, node_ptr++) {
		if (!bit_test(resv_ptr->node_bitmap, i))
			continue;
#ifdef HAVE_BG
		if (cnodes_per_mp)
			cpu_cnt += cnodes_per_mp;
		else
			cpu_cnt += node_ptr->sockets;
#else
		if (slurmctld_conf.fast_schedule)
			cpu_cnt += node_ptr->config_ptr->cpus;
		else
			cpu_cnt += node_ptr->cpus;
#endif
	}
	resv_ptr->cpu_cnt = cpu_cnt;
}

/*
 * _license_validate2 - A variant of license_validate which considers the
 * licenses used by overlapping reservations
 */
static List _license_validate2(resv_desc_msg_t *resv_desc_ptr, bool *valid)
{
	List license_list, merged_list;
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;
	char *merged_licenses;

	license_list = license_validate(resv_desc_ptr->licenses, valid);
	if (!valid || (resv_desc_ptr->licenses == NULL))
		return license_list;

	merged_licenses = xstrdup(resv_desc_ptr->licenses);
	iter = list_iterator_create(resv_list);
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if ((resv_ptr->licenses   == NULL) ||
		    (resv_ptr->end_time   <= resv_desc_ptr->start_time) ||
		    (resv_ptr->start_time >= resv_desc_ptr->end_time))
			continue;	/* No overlap */
		if (resv_desc_ptr->name &&
		    !strcmp(resv_desc_ptr->name, resv_ptr->name))
			continue;	/* Modifying this reservation */
		xstrcat(merged_licenses, ",");
		xstrcat(merged_licenses, resv_ptr->licenses);
	}
	list_iterator_destroy(iter);
	merged_list = license_validate(merged_licenses, valid);
	xfree(merged_licenses);
	FREE_NULL_LIST(merged_list);
	return license_list;
}

/* Create a resource reservation */
extern int create_resv(resv_desc_msg_t *resv_desc_ptr)
{
	int i, j, rc = SLURM_SUCCESS;
	time_t now = time(NULL);
	struct part_record *part_ptr = NULL;
	bitstr_t *node_bitmap = NULL;
	bitstr_t *core_bitmap = NULL;
	slurmctld_resv_t *resv_ptr;
	int account_cnt = 0, user_cnt = 0;
	char **account_list = NULL;
	uid_t *user_list = NULL;
	char start_time[32], end_time[32];
	List license_list = (List) NULL;
	char *name1, *name2, *val1, *val2;
	uint32_t total_node_cnt = 0;
	bool account_not = false, user_not = false;

	if (!resv_list)
		resv_list = list_create(_del_resv_rec);
	_dump_resv_req(resv_desc_ptr, "create_resv");

	/* Validate the request */
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
		resv_desc_ptr->end_time = resv_desc_ptr->start_time + ONE_YEAR;
	} else if (resv_desc_ptr->duration) {
		resv_desc_ptr->end_time = resv_desc_ptr->start_time +
					  (resv_desc_ptr->duration * 60);
	} else
		resv_desc_ptr->end_time = INFINITE;
	if (resv_desc_ptr->flags == NO_VAL)
		resv_desc_ptr->flags = 0;
	else {
		resv_desc_ptr->flags &= RESERVE_FLAG_MAINT    |
					RESERVE_FLAG_OVERLAP  |
					RESERVE_FLAG_IGN_JOBS |
					RESERVE_FLAG_DAILY    |
					RESERVE_FLAG_WEEKLY   |
					RESERVE_FLAG_LIC_ONLY |
					RESERVE_FLAG_STATIC   |
					RESERVE_FLAG_PART_NODES  |
					RESERVE_FLAG_FIRST_CORES |
					RESERVE_FLAG_TIME_FLOAT;
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
	if ((resv_desc_ptr->accounts == NULL) &&
	    (resv_desc_ptr->users == NULL)) {
		info("Reservation request lacks users or accounts");
		rc = ESLURM_INVALID_ACCOUNT;
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
				     &user_cnt, &user_list, &user_not);
		if (rc)
			goto bad_parse;
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

	/* Sort the list of node counts in order descending size */
	if (resv_desc_ptr->node_cnt) {
		for (i = 0; resv_desc_ptr->node_cnt[i]; i++) {
			int max_inx = i;
			for (j = (i + 1); resv_desc_ptr->node_cnt[j]; j++) {
				if (resv_desc_ptr->node_cnt[j] >
				    resv_desc_ptr->node_cnt[max_inx])
					max_inx = j;
			}
			if (max_inx != i) {	/* swap the values */
				uint32_t max_val = resv_desc_ptr->
						   node_cnt[max_inx];
				resv_desc_ptr->node_cnt[max_inx] =
					resv_desc_ptr->node_cnt[i];
				resv_desc_ptr->node_cnt[i] = max_val;
			}
		}
	}

#ifdef HAVE_BG
	if (!cnodes_per_mp) {
		select_g_alter_node_cnt(SELECT_GET_NODE_SCALING,
					&cnodes_per_mp);
	}
	if (resv_desc_ptr->node_cnt && cnodes_per_mp) {
		/* Pack multiple small blocks into midplane rather than
		 * allocating a whole midplane for each small block */
		int small_block_nodes = 0, small_block_count = 0;
		for (i = 0; resv_desc_ptr->node_cnt[i]; i++) {
			if (resv_desc_ptr->node_cnt[i] < cnodes_per_mp)
				small_block_nodes += resv_desc_ptr->node_cnt[i];
		}
		small_block_count  =  small_block_nodes;
		small_block_count += (cnodes_per_mp - 1);
		small_block_count /=  cnodes_per_mp;

		/* Convert c-node count to midplane count */
		total_node_cnt = 0;
		for (i = 0; resv_desc_ptr->node_cnt[i]; i++) {
			if (resv_desc_ptr->node_cnt[i] < cnodes_per_mp) {
				if (!resv_desc_ptr->core_cnt)
					resv_desc_ptr->core_cnt =
						xmalloc(sizeof(uint32_t) * 2);
				resv_desc_ptr->core_cnt[0] +=
					resv_desc_ptr->node_cnt[i];
				if (small_block_count == 0) {
					resv_desc_ptr->node_cnt[i] = 0;
					break;
				}
				small_block_count--;
			}

			resv_desc_ptr->node_cnt[i] += (cnodes_per_mp - 1);
			resv_desc_ptr->node_cnt[i] /=  cnodes_per_mp;
			total_node_cnt += resv_desc_ptr->node_cnt[i];
		}
	}
#endif

	if (resv_desc_ptr->node_list) {
#ifdef HAVE_BG
		int inx;
		bitstr_t *cnode_bitmap = NULL;
		for (inx = 0; resv_desc_ptr->node_list[inx]; inx++) {
			if (resv_desc_ptr->node_list[inx] == '['
			    && resv_desc_ptr->node_list[inx-1] <= '9'
			    && resv_desc_ptr->node_list[inx-1] >= '0') {
				if (!(cnode_bitmap =
				      select_g_ba_cnodelist2bitmap(
					      resv_desc_ptr->node_list+inx))) {
					rc = ESLURM_INVALID_NODE_NAME;
					goto bad_parse;
				}
				resv_desc_ptr->node_list[inx] = '\0';
				break;
			}
		}
#endif

		resv_desc_ptr->flags |= RESERVE_FLAG_SPEC_NODES;
		if (strcasecmp(resv_desc_ptr->node_list, "ALL") == 0) {
			if ((resv_desc_ptr->partition) &&
			    (resv_desc_ptr->flags & RESERVE_FLAG_PART_NODES)) {
				part_ptr = find_part_record(
					resv_desc_ptr->partition);
				node_bitmap = bit_copy(part_ptr->node_bitmap);
			} else {
				node_bitmap = bit_alloc(node_record_count);
				bit_nset(node_bitmap, 0,(node_record_count-1));
			}
			xfree(resv_desc_ptr->node_list);
			resv_desc_ptr->node_list =
				bitmap2node_name(node_bitmap);
		} else if (node_name2bitmap(resv_desc_ptr->node_list,
					    false, &node_bitmap)) {
			rc = ESLURM_INVALID_NODE_NAME;
			goto bad_parse;
		}
		if (bit_set_count(node_bitmap) == 0) {
			info("Reservation node list is empty");
			rc = ESLURM_INVALID_NODE_NAME;
			goto bad_parse;
		}
		if (!(resv_desc_ptr->flags & RESERVE_FLAG_OVERLAP) &&
		    _resv_overlap(resv_desc_ptr->start_time,
				  resv_desc_ptr->end_time,
				  resv_desc_ptr->flags, node_bitmap,
				  NULL)) {
			info("Reservation request overlaps another");
			rc = ESLURM_RESERVATION_OVERLAP;
			goto bad_parse;
		}
		total_node_cnt = bit_set_count(node_bitmap);
		if (!(resv_desc_ptr->flags & RESERVE_FLAG_IGN_JOBS) &&
		    !resv_desc_ptr->core_cnt &&
		    _job_overlap(resv_desc_ptr->start_time,
				 resv_desc_ptr->flags, node_bitmap)) {
			info("Reservation request overlaps jobs");
			rc = ESLURM_NODES_BUSY;
			goto bad_parse;
		}
#ifdef HAVE_BG
		if (cnode_bitmap && total_node_cnt == 1) {
			int offset =
				cr_get_coremap_offset(bit_ffs(node_bitmap));

			if (!cnodes_per_mp)
				select_g_alter_node_cnt(SELECT_GET_NODE_SCALING,
							&cnodes_per_mp);

			if (!core_bitmap)
				core_bitmap = cr_create_cluster_core_bitmap(
					cnodes_per_mp);
			if (!resv_desc_ptr->core_cnt) {
				resv_desc_ptr->core_cnt =
					xmalloc(sizeof(uint32_t) * 2);
				resv_desc_ptr->core_cnt[0] =
					bit_clear_count(cnode_bitmap);
			}
			if (!resv_desc_ptr->node_cnt) {
				resv_desc_ptr->node_cnt =
					xmalloc(sizeof(uint32_t) * 2);
				resv_desc_ptr->node_cnt[0] = 1;
			}

			/* We only have to worry about this one
			   midplane since none of the others will be
			   considered.
			*/
			for (inx=0; inx < cnodes_per_mp; inx++) {
				/* Skip any not set, since they are
				 * the only ones available to run on. */
				if (!bit_test(cnode_bitmap, inx))
					continue;
				bit_set(core_bitmap, inx+offset);
			}
		}
		FREE_NULL_BITMAP(cnode_bitmap);
#endif
		/* We do allow to request cores with nodelist */
		if (resv_desc_ptr->core_cnt) {
			int nodecnt = bit_set_count(node_bitmap);
			int nodeinx = 0;
			while (nodeinx < nodecnt) {
				if (!resv_desc_ptr->core_cnt[nodeinx]) {
					info("Core count for reservation node "
					     "list is not consistent!");
					rc = ESLURM_INVALID_NODE_NAME;
					goto bad_parse;
				}
#if _DEBUG
				info("Requesting %d cores for node_list %d",
				     resv_desc_ptr->core_cnt[nodeinx],
				     nodeinx);
#endif
				nodeinx++;
			}
			rc = _select_nodes(resv_desc_ptr, &part_ptr,
					   &node_bitmap, &core_bitmap);
			if (rc != SLURM_SUCCESS)
				goto bad_parse;
		}
	} else if (!(resv_desc_ptr->flags & RESERVE_FLAG_LIC_ONLY)) {
		if ((!resv_desc_ptr->node_cnt || !resv_desc_ptr->node_cnt[0]) &&
		    !resv_desc_ptr->core_cnt) {
			info("Reservation request lacks node specification");
			rc = ESLURM_INVALID_NODE_NAME;
		} else {
		   rc = _select_nodes(resv_desc_ptr, &part_ptr, &node_bitmap,
				       &core_bitmap);
		}
		if (rc != SLURM_SUCCESS) {
			goto bad_parse;
		}

		/* Get count of allocated nodes, on BlueGene systems, this
		 * might be more than requested */
		total_node_cnt = bit_set_count(node_bitmap);
	}

	if (resv_desc_ptr->core_cnt && !core_bitmap) {
		info("Attempt to reserve cores not possible with current "
		     "configuration");
		rc = ESLURM_INVALID_CPU_COUNT;
		goto bad_parse;
	}

	_generate_resv_id();
	if (resv_desc_ptr->name) {
		resv_ptr = (slurmctld_resv_t *) list_find_first (resv_list,
				_find_resv_name, resv_desc_ptr->name);
		if (resv_ptr) {
			info("Reservation request name duplication (%s)",
			     resv_desc_ptr->name);
			rc = ESLURM_RESERVATION_NAME_DUP;
			goto bad_parse;
		}
	} else {
		while (1) {
			_generate_resv_name(resv_desc_ptr);
			resv_ptr = (slurmctld_resv_t *)
					list_find_first (resv_list,
					_find_resv_name, resv_desc_ptr->name);
			if (!resv_ptr)
				break;
			_generate_resv_id();	/* makes new suffix */
			/* Same as previously created name, retry */
		}
	}

	/* Create a new reservation record */
	resv_ptr = xmalloc(sizeof(slurmctld_resv_t));
	resv_ptr->accounts	= resv_desc_ptr->accounts;
	resv_desc_ptr->accounts = NULL;		/* Nothing left to free */
	resv_ptr->account_cnt	= account_cnt;
	resv_ptr->account_list	= account_list;
	resv_ptr->account_not	= account_not;
	resv_ptr->duration      = resv_desc_ptr->duration;
	resv_ptr->end_time	= resv_desc_ptr->end_time;
	resv_ptr->features	= resv_desc_ptr->features;
	resv_desc_ptr->features = NULL;		/* Nothing left to free */
	resv_ptr->licenses	= resv_desc_ptr->licenses;
	resv_desc_ptr->licenses = NULL;		/* Nothing left to free */
	resv_ptr->license_list	= license_list;
	resv_ptr->resv_id       = top_suffix;
	xassert(resv_ptr->magic = RESV_MAGIC);	/* Sets value */
	resv_ptr->name		= xstrdup(resv_desc_ptr->name);
	resv_ptr->node_cnt	= total_node_cnt;
	resv_ptr->node_list	= resv_desc_ptr->node_list;
	resv_desc_ptr->node_list = NULL;	/* Nothing left to free */
	resv_ptr->node_bitmap	= node_bitmap;	/* May be unset */
	resv_ptr->core_bitmap	= core_bitmap;	/* May be unset */
	resv_ptr->partition	= resv_desc_ptr->partition;
	resv_desc_ptr->partition = NULL;	/* Nothing left to free */
	resv_ptr->part_ptr	= part_ptr;
	resv_ptr->start_time	= resv_desc_ptr->start_time;
	resv_ptr->start_time_first = resv_ptr->start_time;
	resv_ptr->start_time_prev = resv_ptr->start_time;
	resv_ptr->flags		= resv_desc_ptr->flags;
	resv_ptr->users		= resv_desc_ptr->users;
	resv_ptr->user_cnt	= user_cnt;
	resv_ptr->user_list	= user_list;
	resv_ptr->user_not	= user_not;
	resv_desc_ptr->users 	= NULL;		/* Nothing left to free */

	if (!resv_desc_ptr->core_cnt) {
#if _DEBUG
		info("reservation using full nodes");
#endif
		_set_cpu_cnt(resv_ptr);
		resv_ptr->full_nodes = 1;
	} else {
		resv_ptr->cpu_cnt = bit_set_count(resv_ptr->core_bitmap);
#if _DEBUG
		info("reservation using partial nodes: core count %u",
		     resv_ptr->cpu_cnt);
#endif
		resv_ptr->full_nodes = 0;
	}

	if ((rc = _set_assoc_list(resv_ptr)) != SLURM_SUCCESS)
		goto bad_parse;

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
	info("sched: Created reservation %s%s%s%s%s nodes=%s start=%s end=%s",
	     resv_ptr->name, name1, val1, name2, val2,
	     resv_ptr->node_list, start_time, end_time);
	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT)
		resv_ptr->start_time -= now;

	/* This needs to be done after all other setup is done. */
	_post_resv_create(resv_ptr);

	list_append(resv_list, resv_ptr);
	last_resv_update = now;
	schedule_resv_save();

	return SLURM_SUCCESS;

 bad_parse:
	for (i = 0; i < account_cnt; i++)
		xfree(account_list[i]);
	xfree(account_list);
	if (license_list)
		list_destroy(license_list);
	FREE_NULL_BITMAP(node_bitmap);
	FREE_NULL_BITMAP(core_bitmap);
	xfree(user_list);
	return rc;
}

/* Purge all reservation data structures */
extern void resv_fini(void)
{
	if (resv_list) {
		list_destroy(resv_list);
		resv_list = (List) NULL;
	}
}

/* Update an exiting resource reservation */
extern int update_resv(resv_desc_msg_t *resv_desc_ptr)
{
	time_t now = time(NULL);
	slurmctld_resv_t *resv_backup, *resv_ptr;
	int error_code = SLURM_SUCCESS, i, rc;
	char start_time[32], end_time[32];
	char *name1, *name2, *val1, *val2;

	if (!resv_list)
		resv_list = list_create(_del_resv_rec);
	_dump_resv_req(resv_desc_ptr, "update_resv");

	/* Find the specified reservation */
	if (!resv_desc_ptr->name)
		return ESLURM_RESERVATION_INVALID;

	resv_ptr = (slurmctld_resv_t *) list_find_first (resv_list,
			_find_resv_name, resv_desc_ptr->name);
	if (!resv_ptr)
		return ESLURM_RESERVATION_INVALID;

	/* FIXME: Support more core based reservation updates */
	if ((resv_ptr->full_nodes == 0) &&
	    (resv_desc_ptr->node_cnt || resv_desc_ptr->node_list))
		return ESLURM_RESERVATION_NOT_USABLE;

	/* Make backup to restore state in case of failure */
	resv_backup = _copy_resv(resv_ptr);

	/* Process the request */
	if (resv_desc_ptr->flags != NO_VAL) {
		if (resv_desc_ptr->flags & RESERVE_FLAG_MAINT)
			resv_ptr->flags |= RESERVE_FLAG_MAINT;
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_MAINT)
			resv_ptr->flags &= (~RESERVE_FLAG_MAINT);
		if (resv_desc_ptr->flags & RESERVE_FLAG_OVERLAP)
			resv_ptr->flags |= RESERVE_FLAG_OVERLAP;
		if (resv_desc_ptr->flags & RESERVE_FLAG_IGN_JOBS)
			resv_ptr->flags |= RESERVE_FLAG_IGN_JOBS;
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_IGN_JOB)
			resv_ptr->flags &= (~RESERVE_FLAG_IGN_JOBS);
		if (resv_desc_ptr->flags & RESERVE_FLAG_DAILY)
			resv_ptr->flags |= RESERVE_FLAG_DAILY;
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_DAILY)
			resv_ptr->flags &= (~RESERVE_FLAG_DAILY);
		if (resv_desc_ptr->flags & RESERVE_FLAG_WEEKLY)
			resv_ptr->flags |= RESERVE_FLAG_WEEKLY;
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_WEEKLY)
			resv_ptr->flags &= (~RESERVE_FLAG_WEEKLY);
		if (resv_desc_ptr->flags & RESERVE_FLAG_LIC_ONLY)
			resv_ptr->flags |= RESERVE_FLAG_LIC_ONLY;
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_LIC_ONLY)
			resv_ptr->flags &= (~RESERVE_FLAG_LIC_ONLY);
		if (resv_desc_ptr->flags & RESERVE_FLAG_STATIC)
			resv_ptr->flags |= RESERVE_FLAG_STATIC;
		if (resv_desc_ptr->flags & RESERVE_FLAG_NO_STATIC)
			resv_ptr->flags &= (~RESERVE_FLAG_STATIC);
		if (resv_desc_ptr->flags & RESERVE_FLAG_PART_NODES) {
			if ((resv_ptr->partition == NULL) &&
			    (resv_desc_ptr->partition == NULL)) {
				info("Reservation %s request can not set "
				     "Part_Nodes flag without partition",
				     resv_desc_ptr->name);
				error_code = ESLURM_INVALID_PARTITION_NAME;
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
	}
	if (resv_desc_ptr->partition && (resv_desc_ptr->partition[0] == '\0')){
		/* Clear the partition */
		xfree(resv_desc_ptr->partition);
		xfree(resv_ptr->partition);
		resv_ptr->part_ptr = NULL;
	}
	if (resv_desc_ptr->partition) {
		struct part_record *part_ptr = NULL;
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
	if (resv_desc_ptr->licenses && (resv_desc_ptr->licenses[0] == '\0')) {
		if (((resv_desc_ptr->node_cnt != NULL)  &&
		     (resv_desc_ptr->node_cnt[0] == 0)) ||
		    ((resv_desc_ptr->node_cnt == NULL)  &&
		     (resv_ptr->node_cnt == 0))) {
			info("Reservation %s attempt to clear licenses with "
			     "NodeCount=0", resv_desc_ptr->name);
			error_code = ESLURM_INVALID_LICENSES;
			goto update_failure;
		}
		xfree(resv_desc_ptr->licenses);	/* clear licenses */
		xfree(resv_ptr->licenses);
		if (resv_ptr->license_list)
			list_destroy(resv_ptr->license_list);
	}

	if (resv_desc_ptr->licenses) {
		bool valid = true;
		List license_list;
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
		if (resv_ptr->license_list)
			list_destroy(resv_ptr->license_list);
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
	if (resv_desc_ptr->users) {
		rc = _update_uid_list(resv_ptr, resv_desc_ptr->users);
		if (rc) {
			error_code = rc;
			goto update_failure;
		}
	}
	if ((resv_ptr->users == NULL) && (resv_ptr->accounts == NULL)) {
		info("Reservation %s request lacks users or accounts",
		     resv_desc_ptr->name);
		error_code = ESLURM_RESERVATION_EMPTY;
		goto update_failure;
	}

	if (resv_desc_ptr->start_time != (time_t) NO_VAL) {
		if (resv_ptr->start_time <= time(NULL)) {
			info("%s: reservation already started", __func__);
			error_code = ESLURM_INVALID_TIME_VALUE;
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
		resv_ptr->duration = 0;
	}
	if (resv_desc_ptr->duration != NO_VAL) {
		resv_ptr->duration = resv_desc_ptr->duration;
		resv_ptr->end_time = resv_ptr->start_time_first +
				     (resv_desc_ptr->duration * 60);
	}

	if (resv_ptr->start_time >= resv_ptr->end_time) {
		error_code = ESLURM_INVALID_TIME_VALUE;
		goto update_failure;
	}
	if (resv_desc_ptr->node_list &&
	    (resv_desc_ptr->node_list[0] == '\0')) {	/* Clear bitmap */
		resv_ptr->flags &= (~RESERVE_FLAG_SPEC_NODES);
		xfree(resv_desc_ptr->node_list);
		xfree(resv_ptr->node_list);
		FREE_NULL_BITMAP(resv_ptr->node_bitmap);
		FREE_NULL_BITMAP(resv_ptr->core_bitmap);
		resv_ptr->node_bitmap = bit_alloc(node_record_count);
		if ((resv_desc_ptr->node_cnt == NULL) ||
		    (resv_desc_ptr->node_cnt[0] == 0)) {
			xrealloc(resv_desc_ptr->node_cnt, sizeof(uint32_t) * 2);
			resv_desc_ptr->node_cnt[0] = resv_ptr->node_cnt;
			resv_desc_ptr->node_cnt[1] = 0;
		}
		resv_ptr->node_cnt = 0;
	}
	if (resv_desc_ptr->node_list) {		/* Change bitmap last */
		bitstr_t *node_bitmap;
		resv_ptr->flags |= RESERVE_FLAG_SPEC_NODES;
		if (strcasecmp(resv_desc_ptr->node_list, "ALL") == 0) {
			if ((resv_ptr->partition) &&
			    (resv_ptr->flags & RESERVE_FLAG_PART_NODES)) {
				struct part_record *part_ptr = NULL;
				part_ptr = find_part_record(resv_ptr->
							    partition);
				node_bitmap = bit_copy(part_ptr->node_bitmap);
				xfree(resv_ptr->node_list);
				xfree(resv_desc_ptr->node_list);
				resv_ptr->node_list = xstrdup(part_ptr->nodes);
			} else {
				node_bitmap = bit_alloc(node_record_count);
				bit_nset(node_bitmap, 0,(node_record_count-1));
				resv_ptr->flags &= (~RESERVE_FLAG_PART_NODES);
				xfree(resv_ptr->node_list);
				resv_ptr->node_list = resv_desc_ptr->node_list;
			}
		} else {
			resv_ptr->flags &= (~RESERVE_FLAG_PART_NODES);
			if (node_name2bitmap(resv_desc_ptr->node_list,
					    false, &node_bitmap)) {
				error_code = ESLURM_INVALID_NODE_NAME;
				goto update_failure;
			}
			xfree(resv_ptr->node_list);
			resv_ptr->node_list = resv_desc_ptr->node_list;
		}
		resv_desc_ptr->node_list = NULL;  /* Nothing left to free */
		FREE_NULL_BITMAP(resv_ptr->node_bitmap);
		FREE_NULL_BITMAP(resv_ptr->core_bitmap);
		resv_ptr->node_bitmap = node_bitmap;
		resv_ptr->node_cnt = bit_set_count(resv_ptr->node_bitmap);
	}
	if (resv_desc_ptr->node_cnt) {
		uint32_t total_node_cnt = 0;
		resv_ptr->flags &= (~RESERVE_FLAG_PART_NODES);

#ifdef HAVE_BG
		if (!cnodes_per_mp) {
			select_g_alter_node_cnt(SELECT_GET_NODE_SCALING,
						&cnodes_per_mp);
		}
		if (cnodes_per_mp) {
			/* Convert c-node count to midplane count */
			for (i = 0; resv_desc_ptr->node_cnt[i]; i++) {
				resv_desc_ptr->node_cnt[i] += cnodes_per_mp - 1;
				resv_desc_ptr->node_cnt[i] /= cnodes_per_mp;
			}
		}
#endif
		for (i = 0; resv_desc_ptr->node_cnt[i]; i++) {
			total_node_cnt += resv_desc_ptr->node_cnt[i];
		}
		rc = _resize_resv(resv_ptr, total_node_cnt);
		if (rc) {
			error_code = rc;
			goto update_failure;
		}
		resv_ptr->node_cnt = bit_set_count(resv_ptr->node_bitmap);
	}
	if (_resv_overlap(resv_ptr->start_time, resv_ptr->end_time,
			  resv_ptr->flags, resv_ptr->node_bitmap, resv_ptr)) {
		info("Reservation %s request overlaps another",
		     resv_desc_ptr->name);
		error_code = ESLURM_RESERVATION_OVERLAP;
		goto update_failure;
	}
	if (_job_overlap(resv_ptr->start_time, resv_ptr->flags,
			 resv_ptr->node_bitmap)) {
		info("Reservation %s request overlaps jobs",
		     resv_desc_ptr->name);
		error_code = ESLURM_NODES_BUSY;
		goto update_failure;
	}
	_set_cpu_cnt(resv_ptr);

	/* This needs to be after checks for both account and user changes */
	if ((error_code = _set_assoc_list(resv_ptr)) != SLURM_SUCCESS)
		goto update_failure;

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
	info("sched: Updated reservation=%s%s%s%s%s nodes=%s licenses=%s "
	     "start=%s end=%s",
	     resv_ptr->name, name1, val1, name2, val2,
	     resv_ptr->node_list, resv_ptr->licenses, start_time, end_time);

	_post_resv_update(resv_ptr, resv_backup);
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
	ListIterator job_iterator;
	struct job_record *job_ptr;
	bool match = false;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if ((!IS_JOB_FINISHED(job_ptr)) &&
		    (job_ptr->resv_id == resv_ptr->resv_id)) {
			match = true;
			break;
		}
	}
	list_iterator_destroy(job_iterator);

	return match;
}

/* Clear the reservation points for jobs referencing a defunct reservation */
static void _clear_job_resv(slurmctld_resv_t *resv_ptr)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (job_ptr->resv_ptr != resv_ptr)
			continue;
		if (!IS_JOB_FINISHED(job_ptr)) {
			info("Job %u linked to defunct reservation %s, "
			     "clearing that reservation",
			     job_ptr->job_id, job_ptr->resv_name);
		}
		job_ptr->resv_id = 0;
		job_ptr->resv_ptr = NULL;
		xfree(job_ptr->resv_name);
	}
	list_iterator_destroy(job_iterator);
}

static bool _match_user_assoc(char *assoc_str, List assoc_list, bool deny)
{
	ListIterator itr;
	bool found = 0;
	slurmdb_association_rec_t *assoc;
	char tmp_char[1000];

	if (!assoc_str || !assoc_list || !list_count(assoc_list))
		return false;

	itr = list_iterator_create(assoc_list);
	while ((assoc = list_next(itr))) {
		while (assoc) {
			snprintf(tmp_char, sizeof(tmp_char), ",%s%u,",
				 deny ? "-" : "", assoc->id);
			if (strstr(assoc_str, tmp_char)) {
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
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;
	int rc = SLURM_SUCCESS;
	time_t now = time(NULL);

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_RESERVATION)
		info("delete_resv: Name=%s", resv_desc_ptr->name);

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if (strcmp(resv_ptr->name, resv_desc_ptr->name))
			continue;
		if (_is_resv_used(resv_ptr)) {
			rc = ESLURM_RESERVATION_BUSY;
			break;
		}

		if (resv_ptr->flags_set_node) {
			resv_ptr->flags_set_node = false;
			_set_nodes_flags(resv_ptr, now,
					 (NODE_STATE_RES | NODE_STATE_MAINT));
			last_node_update = now;
		}

		rc = _post_resv_delete(resv_ptr);
		_clear_job_resv(resv_ptr);
		list_delete_item(iter);
		break;
	}
	list_iterator_destroy(iter);

	if (!resv_ptr) {
		info("Reservation %s not found for deletion",
		     resv_desc_ptr->name);
		return ESLURM_RESERVATION_INVALID;
	}

	(void) set_node_maint_mode(true);
	last_resv_update = time(NULL);
	schedule_resv_save();
	return rc;
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
extern void show_resv(char **buffer_ptr, int *buffer_size, uid_t uid,
		      uint16_t protocol_version)
{
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;
	uint32_t resv_packed;
	int tmp_offset;
	Buf buffer;
	time_t now = time(NULL);
	List assoc_list = NULL;
	bool check_permissions = false;
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK };

	DEF_TIMERS;

	START_TIMER;
	if (!resv_list)
		resv_list = list_create(_del_resv_rec);

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf(BUF_SIZE);

	/* write header: version and time */
	resv_packed = 0;
	pack32(resv_packed, buffer);
	pack_time(now, buffer);

	/* Create this list once since it will not change durning this call. */
	if ((slurmctld_conf.private_data & PRIVATE_DATA_RESERVATIONS)
	    && !validate_operator(uid)) {
		slurmdb_association_rec_t assoc;

		check_permissions = true;

		memset(&assoc, 0, sizeof(slurmdb_association_rec_t));
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
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if (check_permissions) {
			/* Determine if we have access */
			if ((accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS)
			    && resv_ptr->assoc_list) {
				/* Check to see if the association is
				 * here or the parent association is
				 * listed in the valid associations.
				 */
				if (strchr(resv_ptr->assoc_list, '-')) {
					if (_match_user_assoc(
						    resv_ptr->assoc_list,
						    assoc_list,
						    true))
						continue;
				}

				if (strstr(resv_ptr->assoc_list, ",1") ||
				    strstr(resv_ptr->assoc_list, ",2") ||
				    strstr(resv_ptr->assoc_list, ",3") ||
				    strstr(resv_ptr->assoc_list, ",4") ||
				    strstr(resv_ptr->assoc_list, ",5") ||
				    strstr(resv_ptr->assoc_list, ",6") ||
				    strstr(resv_ptr->assoc_list, ",7") ||
				    strstr(resv_ptr->assoc_list, ",8") ||
				    strstr(resv_ptr->assoc_list, ",9") ||
				    strstr(resv_ptr->assoc_list, ",0")) {
					if (!_match_user_assoc(
						    resv_ptr->assoc_list,
						    assoc_list, false))
						continue;
				}
			} else {
				int i = 0;
				for (i = 0; i < resv_ptr->user_cnt; i++) {
					if (resv_ptr->user_list[i] == uid)
						break;
				}

				if (i >= resv_ptr->user_cnt)
					continue;
			}
		}

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

	*buffer_size = get_buf_offset(buffer);
	buffer_ptr[0] = xfer_buf_data(buffer);
	END_TIMER2("show_resv");
}

/* Save the state of all reservations to file */
extern int dump_all_resv_state(void)
{
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;
	int error_code = 0, log_fd;
	char *old_file, *new_file, *reg_file;
	/* Locks: Read node */
	slurmctld_lock_t resv_read_lock =
	    { READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };
	Buf buffer = init_buf(BUF_SIZE);
	DEF_TIMERS;

	START_TIMER;
	if (!resv_list)
		resv_list = list_create(_del_resv_rec);

	/* write header: time */
	packstr(RESV_STATE_VERSION, buffer);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack32(top_suffix, buffer);

	/* write reservation records to buffer */
	lock_slurmctld(resv_read_lock);
	iter = list_iterator_create(resv_list);
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter)))
		_pack_resv(resv_ptr, buffer, true, SLURM_PROTOCOL_VERSION);
	list_iterator_destroy(iter);

	old_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(old_file, "/resv_state.old");
	reg_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(reg_file, "/resv_state");
	new_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(new_file, "/resv_state.new");
	unlock_slurmctld(resv_read_lock);

	/* write the buffer to file */
	lock_state_files();
	log_fd = creat(new_file, 0600);
	if (log_fd < 0) {
		error("Can't save state, error creating file %s, %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount, rc;
		char *data = (char *)get_buf_data(buffer);

		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}
		rc = fsync_and_close(log_fd, "reservation");
		if (rc && !error_code)
			error_code = rc;
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink(reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	unlock_state_files();

	free_buf(buffer);
	END_TIMER2("dump_all_resv_state");
	return 0;
}

/* Validate one reservation record, return true if good */
static bool _validate_one_reservation(slurmctld_resv_t *resv_ptr)
{
	bool account_not = false, user_not = false;

	if ((resv_ptr->name == NULL) || (resv_ptr->name[0] == '\0')) {
		error("Read reservation without name");
		return false;
	}
	if (resv_ptr->partition) {
		struct part_record *part_ptr = NULL;
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
		resv_ptr->account_not  = account_not;
	}
	if (resv_ptr->licenses) {
		bool valid = true;
		if (resv_ptr->license_list)
			list_destroy(resv_ptr->license_list);
		resv_ptr->license_list = license_validate(resv_ptr->licenses,
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
				     &user_cnt, &user_list, &user_not);
		if (rc) {
			error("Reservation %s has invalid users (%s)",
			      resv_ptr->name, resv_ptr->users);
			return false;
		}
		xfree(resv_ptr->user_list);
		resv_ptr->user_cnt  = user_cnt;
		resv_ptr->user_list = user_list;
		resv_ptr->user_not  = user_not;
	}
	if ((resv_ptr->flags & RESERVE_FLAG_PART_NODES) &&
	    resv_ptr->part_ptr && resv_ptr->part_ptr->node_bitmap) {
		xfree(resv_ptr->node_list);
		resv_ptr->node_list = xstrdup(resv_ptr->part_ptr->nodes);
		FREE_NULL_BITMAP(resv_ptr->node_bitmap);
		resv_ptr->node_bitmap = bit_copy(resv_ptr->part_ptr->
						 node_bitmap);
		resv_ptr->node_cnt = bit_set_count(resv_ptr->node_bitmap);
		_set_cpu_cnt(resv_ptr);
		last_resv_update = time(NULL);
	} else if (resv_ptr->node_list) {	/* Change bitmap last */
		bitstr_t *node_bitmap;
#ifdef HAVE_BG
		int inx;
		char save = '\0';
		/* Make sure we take off the cnodes in the reservation */
		for (inx = 0; resv_ptr->node_list[inx]; inx++) {
			if (resv_ptr->node_list[inx] == '['
			    && resv_ptr->node_list[inx-1] <= '9'
			    && resv_ptr->node_list[inx-1] >= '0') {
				save = resv_ptr->node_list[inx];
				resv_ptr->node_list[inx] = '\0';
				break;
			}
		}
#endif
		if (strcasecmp(resv_ptr->node_list, "ALL") == 0) {
			node_bitmap = bit_alloc(node_record_count);
			bit_nset(node_bitmap, 0, (node_record_count - 1));
		} else if (node_name2bitmap(resv_ptr->node_list,
					    false, &node_bitmap)) {
			error("Reservation %s has invalid nodes (%s)",
			      resv_ptr->name, resv_ptr->node_list);
			return false;
		}

#ifdef HAVE_BG
		if (save)
			resv_ptr->node_list[inx] = save;
#endif

		FREE_NULL_BITMAP(resv_ptr->node_bitmap);
		resv_ptr->node_bitmap = node_bitmap;
	}
	return true;
}

/*
 * Validate all reservation records, reset bitmaps, etc.
 * Purge any invalid reservation.
 */
static void _validate_all_reservations(void)
{
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;
	struct job_record *job_ptr;
	char *tmp;
	uint32_t res_num;

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if (!_validate_one_reservation(resv_ptr)) {
			error("Purging invalid reservation record %s",
			      resv_ptr->name);
			_post_resv_delete(resv_ptr);
			_clear_job_resv(resv_ptr);
			list_delete_item(iter);
		} else {
			_set_assoc_list(resv_ptr);
			tmp = strrchr(resv_ptr->name, '_');
			if (tmp) {
				res_num = atoi(tmp + 1);
				top_suffix = MAX(top_suffix, res_num);
			}
		}
	}
	list_iterator_destroy(iter);

	/* Validate all job reservation pointers */
	iter = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(iter))) {
		if (job_ptr->resv_name == NULL)
			continue;

		if ((job_ptr->resv_ptr == NULL) ||
		    (job_ptr->resv_ptr->magic != RESV_MAGIC)) {
			job_ptr->resv_ptr = (slurmctld_resv_t *)
					list_find_first(resv_list,
							_find_resv_name,
							job_ptr->resv_name);
		}
		if (!job_ptr->resv_ptr) {
			error("JobId %u linked to defunct reservation %s",
			       job_ptr->job_id, job_ptr->resv_name);
			job_ptr->resv_id = 0;
			xfree(job_ptr->resv_name);
		}
	}
	list_iterator_destroy(iter);

}

/*
 * Validate that the reserved nodes are not DOWN or DRAINED and
 *	select different nodes as needed.
 */
static void _validate_node_choice(slurmctld_resv_t *resv_ptr)
{
	bitstr_t *tmp_bitmap = NULL;
	bitstr_t *core_bitmap = NULL;
	int i;
	resv_desc_msg_t resv_desc;

	if (resv_ptr->flags & RESERVE_FLAG_SPEC_NODES ||
	    resv_ptr->flags & RESERVE_FLAG_STATIC)
		return;

	i = bit_overlap(resv_ptr->node_bitmap, avail_node_bitmap);
	if (i == resv_ptr->node_cnt)
		return;

	/* Reservation includes DOWN, DRAINED/DRAINING, FAILING or
	 * NO_RESPOND nodes. Generate new request using _select_nodes()
	 * in attempt to replace this nodes */
	memset(&resv_desc, 0, sizeof(resv_desc_msg_t));
	resv_desc.start_time = resv_ptr->start_time;
	resv_desc.end_time   = resv_ptr->end_time;
	resv_desc.features   = resv_ptr->features;
	resv_desc.node_cnt   = xmalloc(sizeof(uint32_t) * 2);
	resv_desc.node_cnt[0]= resv_ptr->node_cnt - i;
	i = _select_nodes(&resv_desc, &resv_ptr->part_ptr, &tmp_bitmap,
			  &core_bitmap);
	xfree(resv_desc.node_cnt);
	xfree(resv_desc.node_list);
	xfree(resv_desc.partition);
	if (i == SLURM_SUCCESS) {
		bit_and(resv_ptr->node_bitmap, avail_node_bitmap);
		bit_or(resv_ptr->node_bitmap, tmp_bitmap);
		FREE_NULL_BITMAP(tmp_bitmap);
		FREE_NULL_BITMAP(resv_ptr->core_bitmap);
		resv_ptr->core_bitmap = core_bitmap;
		xfree(resv_ptr->node_list);
		resv_ptr->node_list = bitmap2node_name(resv_ptr->node_bitmap);
		info("modified reservation %s due to unusable nodes, "
		     "new nodes: %s", resv_ptr->name, resv_ptr->node_list);
	} else if (difftime(resv_ptr->start_time, time(NULL)) < 600) {
		info("reservation %s contains unusable nodes, "
		     "can't reallocate now", resv_ptr->name);
	} else {
		debug("reservation %s contains unusable nodes, "
		      "can't reallocate now", resv_ptr->name);
	}
}

/* Open the reservation state save file, or backup if necessary.
 * state_file IN - the name of the state save file used
 * RET the file description to read from or error code
 */
static int _open_resv_state_file(char **state_file)
{
	int state_fd;
	struct stat stat_buf;

	*state_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(*state_file, "/resv_state");
	state_fd = open(*state_file, O_RDONLY);
	if (state_fd < 0) {
		error("Could not open reservation state file %s: %m",
		      *state_file);
	} else if (fstat(state_fd, &stat_buf) < 0) {
		error("Could not stat reservation state file %s: %m",
		      *state_file);
		(void) close(state_fd);
	} else if (stat_buf.st_size < 10) {
		error("Reservation state file %s too small", *state_file);
		(void) close(state_fd);
	} else 	/* Success */
		return state_fd;

	error("NOTE: Trying backup state save file. Reservations may be lost");
	xstrcat(*state_file, ".old");
	state_fd = open(*state_file, O_RDONLY);
	return state_fd;
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
	char *state_file, *data = NULL, *ver_str = NULL;
	time_t now;
	uint32_t data_size = 0, uint32_tmp;
	int data_allocated, data_read = 0, error_code = 0, state_fd;
	Buf buffer;
	slurmctld_resv_t *resv_ptr = NULL;
	uint16_t protocol_version = (uint16_t) NO_VAL;

	last_resv_update = time(NULL);
	if ((recover == 0) && resv_list) {
		_validate_all_reservations();
		return SLURM_SUCCESS;
	}

	/* Read state file and validate */
	if (resv_list)
		list_flush(resv_list);
	else
		resv_list = list_create(_del_resv_rec);

	/* read the file */
	lock_state_files();
	state_fd = _open_resv_state_file(&state_file);
	if (state_fd < 0) {
		info("No reservation state file (%s) to recover",
		     state_file);
		error_code = ENOENT;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					BUF_SIZE);
			if (data_read < 0) {
				if  (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
						state_file);
					break;
				}
			} else if (data_read == 0)     /* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);
	unlock_state_files();

	buffer = create_buf(data, data_size);

	safe_unpackstr_xmalloc( &ver_str, &uint32_tmp, buffer);
	debug3("Version string in resv_state header is %s", ver_str);
	if (ver_str) {
		if (!strcmp(ver_str, RESV_STATE_VERSION))
			safe_unpack16(&protocol_version, buffer);
		else if (!strcmp(ver_str, RESV_2_6_STATE_VERSION))
			protocol_version = SLURM_2_6_PROTOCOL_VERSION;
	}
	if (protocol_version == (uint16_t) NO_VAL) {
		error("************************************************************");
		error("Can not recover reservation state, data version incompatible");
		error("************************************************************");
		xfree(ver_str);
		free_buf(buffer);
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

		list_append(resv_list, resv_ptr);
		info("Recovered state of reservation %s", resv_ptr->name);
	}

	_validate_all_reservations();
	info("Recovered state of %d reservations", list_count(resv_list));
	free_buf(buffer);
	return error_code;

      unpack_error:
	_validate_all_reservations();
	if (state_fd >= 0)
		error("Incomplete reservation data checkpoint file");
	info("Recovered state of %d reservations", list_count(resv_list));
	if (resv_ptr)
		_del_resv_rec(resv_ptr);
	free_buf(buffer);
	return EFAULT;
}

/*
 * Determine if a job request can use the specified reservations
 *
 * IN/OUT job_ptr - job to validate, set its resv_id
 * RET SLURM_SUCCESS or error code (not found or access denied)
 */
extern int validate_job_resv(struct job_record *job_ptr)
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

	/* Find the named reservation */
	resv_ptr = (slurmctld_resv_t *) list_find_first (resv_list,
			_find_resv_name, job_ptr->resv_name);
	rc = _valid_job_access_resv(job_ptr, resv_ptr);
	if (rc == SLURM_SUCCESS) {
		job_ptr->resv_id    = resv_ptr->resv_id;
		job_ptr->resv_ptr   = resv_ptr;
		_validate_node_choice(resv_ptr);
	}
	return rc;
}

static int  _resize_resv(slurmctld_resv_t *resv_ptr, uint32_t node_cnt)
{
	bitstr_t *tmp1_bitmap = NULL, *tmp2_bitmap = NULL;
	bitstr_t *core_bitmap = NULL;
	int delta_node_cnt, i;
	resv_desc_msg_t resv_desc;

	delta_node_cnt = resv_ptr->node_cnt - node_cnt;
	if (delta_node_cnt == 0)	/* Already correct node count */
		return SLURM_SUCCESS;

	if (delta_node_cnt > 0) {	/* Must decrease node count */
		if (bit_overlap(resv_ptr->node_bitmap, idle_node_bitmap)) {
			/* Start by eliminating idle nodes from reservation */
			tmp1_bitmap = bit_copy(resv_ptr->node_bitmap);
			bit_and(tmp1_bitmap, idle_node_bitmap);
			i = bit_set_count(tmp1_bitmap);
			if (i > delta_node_cnt) {
				tmp2_bitmap = bit_pick_cnt(tmp1_bitmap,
							   delta_node_cnt);
				bit_not(tmp2_bitmap);
				bit_and(resv_ptr->node_bitmap, tmp2_bitmap);
				FREE_NULL_BITMAP(tmp1_bitmap);
				FREE_NULL_BITMAP(tmp2_bitmap);
				delta_node_cnt = 0;	/* ALL DONE */
			} else if (i) {
				bit_not(idle_node_bitmap);
				bit_and(resv_ptr->node_bitmap,
					idle_node_bitmap);
				bit_not(idle_node_bitmap);
				resv_ptr->node_cnt = bit_set_count(
						resv_ptr->node_bitmap);
				delta_node_cnt = resv_ptr->node_cnt -
						 node_cnt;
			}
			FREE_NULL_BITMAP(tmp1_bitmap);
		}
		if (delta_node_cnt > 0) {
			/* Now eliminate allocated nodes from reservation */
			tmp1_bitmap = bit_pick_cnt(resv_ptr->node_bitmap,
						   node_cnt);
			FREE_NULL_BITMAP(resv_ptr->node_bitmap);
			resv_ptr->node_bitmap = tmp1_bitmap;
		}
		xfree(resv_ptr->node_list);
		resv_ptr->node_list = bitmap2node_name(resv_ptr->node_bitmap);
		resv_ptr->node_cnt = node_cnt;
		return SLURM_SUCCESS;
	}

	/* Must increase node count. Make this look like new request so
	 * we can use _select_nodes() for selecting the nodes */
	memset(&resv_desc, 0, sizeof(resv_desc_msg_t));
	resv_desc.start_time = resv_ptr->start_time;
	resv_desc.end_time   = resv_ptr->end_time;
	resv_desc.features   = resv_ptr->features;
	resv_desc.flags      = resv_ptr->flags;
	resv_desc.node_cnt   = xmalloc(sizeof(uint32_t) * 2);
	resv_desc.node_cnt[0]= 0 - delta_node_cnt;
	i = _select_nodes(&resv_desc, &resv_ptr->part_ptr, &tmp1_bitmap,
			  &core_bitmap);
	xfree(resv_desc.node_cnt);
	xfree(resv_desc.node_list);
	xfree(resv_desc.partition);
	if (i == SLURM_SUCCESS) {
		bit_or(resv_ptr->node_bitmap, tmp1_bitmap);
		FREE_NULL_BITMAP(tmp1_bitmap);
		FREE_NULL_BITMAP(resv_ptr->core_bitmap);
		resv_ptr->core_bitmap = core_bitmap;
		xfree(resv_ptr->node_list);
		resv_ptr->node_list = bitmap2node_name(resv_ptr->node_bitmap);
		resv_ptr->node_cnt = node_cnt;
	}
	return i;
}

/* Given a reservation create request, select appropriate nodes for use */
static int  _select_nodes(resv_desc_msg_t *resv_desc_ptr,
			  struct part_record **part_ptr,
			  bitstr_t **resv_bitmap,
			  bitstr_t **core_bitmap)
{
	slurmctld_resv_t *resv_ptr;
	bitstr_t *node_bitmap;
	ListIterator iter;
	int i, rc = SLURM_SUCCESS;
	time_t start_relative, end_relative;
	time_t now = time(NULL);

	if (*part_ptr == NULL) {
		*part_ptr = default_part_loc;
		if (*part_ptr == NULL)
			return ESLURM_DEFAULT_PARTITION_NOT_SET;
		xfree(resv_desc_ptr->partition);	/* should be no-op */
		resv_desc_ptr->partition = xstrdup((*part_ptr)->name);
	}

	/* Start with all nodes in the partition */
	if (*resv_bitmap)
		node_bitmap = bit_copy(*resv_bitmap);
	else
		node_bitmap = bit_copy((*part_ptr)->node_bitmap);

	/* Don't use node already reserved */
	if (!(resv_desc_ptr->flags & RESERVE_FLAG_MAINT) &&
	    !(resv_desc_ptr->flags & RESERVE_FLAG_OVERLAP)) {
		iter = list_iterator_create(resv_list);
		while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
			if (resv_ptr->end_time <= now)
				_advance_resv_time(resv_ptr);
			if (resv_ptr->node_bitmap == NULL)
				continue;
			if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT) {
				start_relative = resv_ptr->start_time + now;
				if (resv_ptr->duration == INFINITE)
					end_relative = start_relative +
						ONE_YEAR;
				else if (resv_ptr->duration &&
					 (resv_ptr->duration != NO_VAL)) {
					end_relative = start_relative +
						       resv_ptr->duration * 60;
				} else {
					end_relative = resv_ptr->end_time;
					if (start_relative > end_relative)
						start_relative = end_relative;
				}
			} else {
				start_relative = resv_ptr->start_time_first;
				end_relative = resv_ptr->end_time;
			}

			if ((start_relative >= resv_desc_ptr->end_time) ||
			    (end_relative   <= resv_desc_ptr->start_time))
				continue;
			if (!resv_ptr->core_bitmap && !resv_ptr->full_nodes) {
				error("Reservation has no core_bitmap and "
				      "full_nodes is zero");
				resv_ptr->full_nodes = 1;
			}
			if (resv_ptr->full_nodes || !resv_desc_ptr->core_cnt) {
				bit_not(resv_ptr->node_bitmap);
				bit_and(node_bitmap, resv_ptr->node_bitmap);
				bit_not(resv_ptr->node_bitmap);
			} else {
				int core_mult = 1;
#ifdef HAVE_BG
				if (!cnodes_per_mp)
					select_g_alter_node_cnt(
						SELECT_GET_NODE_SCALING,
						&cnodes_per_mp);
				core_mult = cnodes_per_mp;
#endif

				if (!*core_bitmap)
					*core_bitmap =
						cr_create_cluster_core_bitmap(
							core_mult);
				bit_or(*core_bitmap, resv_ptr->core_bitmap);
			}
		}
		list_iterator_destroy(iter);
	}

	/* Satisfy feature specification */
	if (resv_desc_ptr->features) {
		int   op_code = FEATURE_OP_AND, last_op_code = FEATURE_OP_AND;
		char *features = xstrdup(resv_desc_ptr->features);
		char *sep_ptr, *token = features;
		bitstr_t *feature_bitmap = bit_copy(node_bitmap);
		struct features_record *feature_ptr;
		ListIterator feature_iter;
		bool match;

		while (1) {
			for (i=0; ; i++) {
				if (token[i] == '\0') {
					sep_ptr = NULL;
					break;
				} else if (token[i] == '|') {
					op_code = FEATURE_OP_OR;
					token[i] = '\0';
					sep_ptr = &token[i];
					break;
				} else if ((token[i] == '&') ||
					   (token[i] == ',')) {
					op_code = FEATURE_OP_AND;
					token[i] = '\0';
					sep_ptr = &token[i];
					break;
				}
			}

			match = false;
			feature_iter = list_iterator_create(feature_list);
			while ((feature_ptr = (struct features_record *)
					list_next(feature_iter))) {
				if (strcmp(token, feature_ptr->name))
					continue;
				if (last_op_code == FEATURE_OP_OR) {
					bit_or(feature_bitmap,
					       feature_ptr->node_bitmap);
				} else {
					bit_and(feature_bitmap,
						feature_ptr->node_bitmap);
				}
				match = true;
				break;
			}
			list_iterator_destroy(feature_iter);
			if (!match) {
				info("reservation feature invalid: %s", token);
				rc = ESLURM_INVALID_FEATURE;
				bit_nclear(feature_bitmap, 0,
					   (node_record_count - 1));
				break;
			}
			if (sep_ptr == NULL)
				break;
			token = sep_ptr + 1;
			last_op_code = op_code;
		}
		xfree(features);
		bit_and(node_bitmap, feature_bitmap);
		FREE_NULL_BITMAP(feature_bitmap);
	}

	if ((resv_desc_ptr->flags & RESERVE_FLAG_MAINT) == 0) {
		/* Nodes must be available */
		bit_and(node_bitmap, avail_node_bitmap);
	}

	/* If *resv_bitmap exists we probably don't need to delete it, when it
	 * gets created off of node_bitmap it will be the same, but just to be
	 * safe we do. */
	FREE_NULL_BITMAP(*resv_bitmap);
	if (rc == SLURM_SUCCESS) {
		/* Free node_list here since it could be filled in in the
		   select plugin.
		*/
		xfree(resv_desc_ptr->node_list);
		*resv_bitmap = _pick_idle_nodes(node_bitmap,
						resv_desc_ptr, core_bitmap);
	}
	FREE_NULL_BITMAP(node_bitmap);
	if (*resv_bitmap == NULL) {
		if (rc == SLURM_SUCCESS)
			rc = ESLURM_NODES_BUSY;
		return rc;
	}

	if (!resv_desc_ptr->node_list)
		resv_desc_ptr->node_list = bitmap2node_name(*resv_bitmap);

	return SLURM_SUCCESS;
}

static bitstr_t *_pick_idle_nodes(bitstr_t *avail_bitmap,
				  resv_desc_msg_t *resv_desc_ptr,
				  bitstr_t **core_bitmap)
{
	int i;
	bitstr_t *ret_bitmap = NULL, *tmp_bitmap;
	uint32_t total_node_cnt = 0;
	bool resv_debug;

#ifdef HAVE_BG
	static uint16_t static_blocks = (uint16_t)NO_VAL;
	if (static_blocks == (uint16_t)NO_VAL) {
		/* Since this never changes we can just set it once
		 * and not look at it again. */
		select_g_get_info_from_plugin(SELECT_STATIC_PART, NULL,
					      &static_blocks);
	}
#else
	static uint16_t static_blocks = 0;
#endif

	if (resv_desc_ptr->node_cnt == NULL) {
		return _pick_idle_node_cnt(avail_bitmap, resv_desc_ptr, 0,
					   core_bitmap);
	} else if ((resv_desc_ptr->node_cnt[0] == 0) ||
		   (resv_desc_ptr->node_cnt[1] == 0)) {
		return _pick_idle_node_cnt(avail_bitmap, resv_desc_ptr,
					   resv_desc_ptr->node_cnt[0],
					   core_bitmap);
	}

	/* Try to create a single reservation that can contain all blocks
	 * unless we have static blocks on a BlueGene system */
	if (static_blocks != 0) {
		for (i = 0; resv_desc_ptr->node_cnt[i]; i++)
			total_node_cnt += resv_desc_ptr->node_cnt[i];
		tmp_bitmap = _pick_idle_node_cnt(avail_bitmap, resv_desc_ptr,
						 total_node_cnt, core_bitmap);
		if (tmp_bitmap) {
			if (total_node_cnt == bit_set_count(tmp_bitmap))
				return tmp_bitmap;
			/* Oversized allocation, possibly due to BlueGene block
			 * size limitations. Need to create as multiple
			 * blocks */
			FREE_NULL_BITMAP(tmp_bitmap);
		}
	}

	/* Need to create reservation containing multiple blocks */
	resv_debug = slurmctld_conf.debug_flags & DEBUG_FLAG_RESERVATION;
	for (i = 0; resv_desc_ptr->node_cnt[i]; i++) {
		tmp_bitmap = _pick_idle_node_cnt(avail_bitmap, resv_desc_ptr,
						 resv_desc_ptr->node_cnt[i],
						 core_bitmap);
		if (tmp_bitmap == NULL) {	/* allocation failure */
			if (resv_debug) {
				info("reservation of %u nodes failed",
				     resv_desc_ptr->node_cnt[i]);
			}
			FREE_NULL_BITMAP(ret_bitmap);
			return NULL;
		}
		if (resv_debug) {
			char *tmp_name;
			tmp_name = bitmap2node_name(tmp_bitmap);
			info("reservation of %u nodes, using %s",
			     resv_desc_ptr->node_cnt[i], tmp_name);
			xfree(tmp_name);
		}
		if (ret_bitmap)
			bit_or(ret_bitmap, tmp_bitmap);
		else
			ret_bitmap = bit_copy(tmp_bitmap);
		bit_not(tmp_bitmap);
		bit_and(avail_bitmap, tmp_bitmap);
		FREE_NULL_BITMAP(tmp_bitmap);
	}

	return ret_bitmap;
}

static void _check_job_compatibility(struct job_record *job_ptr,
				     bitstr_t *avail_bitmap,
				     bitstr_t **core_bitmap)
{
	uint32_t total_nodes;
	bitstr_t *full_node_bitmap;
	int i_core, i_node, core_mult = 1;
	int start = 0;
	int rep_count = 0;
	job_resources_t *job_res = job_ptr->job_resrcs;

	if (!job_res->core_bitmap)
		return;

	total_nodes = bit_set_count(job_res->node_bitmap);

#if _DEBUG
{
	char str[200];
	bit_fmt(str, sizeof(str), job_res->core_bitmap);
	info("Checking %d nodes (of %d) for job %u, "
	     "core_bitmap:%s core_bitmap_size:%d",
	     total_nodes, bit_size(job_res->node_bitmap),
	     job_ptr->job_id, str, bit_size(job_res->core_bitmap));
}
#endif

#ifdef HAVE_BG
	if (!cnodes_per_mp)
		select_g_alter_node_cnt(SELECT_GET_NODE_SCALING,
					&cnodes_per_mp);
	core_mult = cnodes_per_mp;
#endif

	full_node_bitmap = bit_copy(job_res->node_bitmap);

	if (!*core_bitmap)
		*core_bitmap = cr_create_cluster_core_bitmap(core_mult);

	i_node = 0;
	while (i_node < total_nodes) {
		int cores_in_a_node = (job_res->sockets_per_node[i_node] *
				       job_res->cores_per_socket[i_node]);

		int repeat_node_conf = job_res->sock_core_rep_count[rep_count++];
		int node_bitmap_inx;

#if _DEBUG
		info("Working with %d cores per node. Same node conf repeated "
		     "%d times (start core offset %d)",
		     cores_in_a_node, repeat_node_conf, start);
#endif

		i_node += repeat_node_conf;

		while (repeat_node_conf--) {
			int allocated;
			int global_core_start;

			node_bitmap_inx = bit_ffs(full_node_bitmap);
			global_core_start =
				cr_get_coremap_offset(node_bitmap_inx);
			allocated = 0;

			for (i_core=0;i_core < cores_in_a_node;i_core++) {
#if _DEBUG
				info("i_core: %d, start: %d, allocated: %d",
				     i_core, start, allocated);
#endif
				if (bit_test(job_ptr->job_resrcs->core_bitmap,
					     i_core + start)) {
					allocated++;
				bit_set(*core_bitmap,
					global_core_start + i_core);
				}
			}
#if _DEBUG
			info("Checking node %d, allocated: %d, "
			     "cores_in_a_node: %d", node_bitmap_inx,
			     allocated, cores_in_a_node);
#endif
			if (allocated == cores_in_a_node) {
				/* We can exclude this node */
#if _DEBUG
				info("Excluding node %d", node_bitmap_inx);
#endif
				bit_clear(avail_bitmap, node_bitmap_inx);
			}
			start += cores_in_a_node;
			bit_clear(full_node_bitmap, node_bitmap_inx);
		}
	}
	FREE_NULL_BITMAP(full_node_bitmap);
}

static bitstr_t *_pick_idle_node_cnt(bitstr_t *avail_bitmap,
				     resv_desc_msg_t *resv_desc_ptr,
				     uint32_t node_cnt, bitstr_t **core_bitmap)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	bitstr_t *orig_bitmap, *save_bitmap = NULL;
	bitstr_t *ret_bitmap = NULL, *tmp_bitmap;
	int total_node_cnt;

	total_node_cnt = bit_set_count(avail_bitmap);
	if (total_node_cnt < node_cnt) {
		verbose("reservation requests more nodes than are available");
		return NULL;
	} else if ((total_node_cnt == node_cnt) &&
		   (resv_desc_ptr->flags & RESERVE_FLAG_IGN_JOBS)) {
		return select_g_resv_test(resv_desc_ptr, node_cnt,
					  avail_bitmap, core_bitmap);
	} else if ((node_cnt == 0) &&
		   ((resv_desc_ptr->core_cnt == NULL) ||
		    (resv_desc_ptr->core_cnt[0] == 0)) &&
		   (resv_desc_ptr->flags & RESERVE_FLAG_LIC_ONLY)) {
		return bit_alloc(bit_size(avail_bitmap));
	}

	orig_bitmap = bit_copy(avail_bitmap);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr))
			continue;
		if (job_ptr->end_time < resv_desc_ptr->start_time)
			continue;

		if (!resv_desc_ptr->core_cnt) {
			bit_not(job_ptr->node_bitmap);
			bit_and(avail_bitmap, job_ptr->node_bitmap);
			bit_not(job_ptr->node_bitmap);
		} else {
			_check_job_compatibility(job_ptr, avail_bitmap,
						 core_bitmap);
		}
	}
	list_iterator_destroy(job_iterator);

	total_node_cnt = bit_set_count(avail_bitmap);
	if (total_node_cnt >= node_cnt) {
		/* NOTE: select_g_resv_test() does NOT preserve avail_bitmap,
		 * so we do that here and other calls to that function */
		save_bitmap = bit_copy(avail_bitmap);
		ret_bitmap = select_g_resv_test(resv_desc_ptr, node_cnt,
						avail_bitmap, core_bitmap);
		if (ret_bitmap)
			goto fini;
		bit_or(avail_bitmap, save_bitmap);
		FREE_NULL_BITMAP(save_bitmap);
	}

	/* Next: Try to reserve nodes that will be allocated to a limited
	 * number of running jobs. We could sort the jobs by priority, QOS,
	 * size or other criterion if desired. Right now we just go down
	 * the unsorted job list. */
	if (resv_desc_ptr->flags & RESERVE_FLAG_IGN_JOBS) {
		job_iterator = list_iterator_create(job_list);
		while ((job_ptr = (struct job_record *)
			list_next(job_iterator))) {
			if (!IS_JOB_RUNNING(job_ptr) &&
			    !IS_JOB_SUSPENDED(job_ptr))
				continue;
			if (job_ptr->end_time < resv_desc_ptr->start_time)
				continue;
			tmp_bitmap = bit_copy(orig_bitmap);
			bit_and(tmp_bitmap, job_ptr->node_bitmap);
			if (bit_set_count(tmp_bitmap) > 0)
				bit_or(avail_bitmap, tmp_bitmap);
			total_node_cnt = bit_set_count(avail_bitmap);
			if (total_node_cnt >= node_cnt) {
				save_bitmap = bit_copy(avail_bitmap);
				ret_bitmap = select_g_resv_test(
					resv_desc_ptr, node_cnt,
					avail_bitmap, core_bitmap);
				if (!ret_bitmap) {
					bit_or(avail_bitmap, save_bitmap);
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
#if _DEBUG
	if (ret_bitmap) {
		char str[300];
		bit_fmt(str, (sizeof(str) - 1), ret_bitmap);
		info("_pick_idle_node_cnt: node bitmap:%s", str);
		if (*core_bitmap) {
			bit_fmt(str, (sizeof(str) - 1), *core_bitmap);
			info("_pick_idle_node_cnt: core bitmap:%s", str);
		}
	}
#endif
	return ret_bitmap;
}

/* Determine if a job has access to a reservation
 * RET SLURM_SUCCESS if true, some error code otherwise */
static int _valid_job_access_resv(struct job_record *job_ptr,
				  slurmctld_resv_t *resv_ptr)
{
	bool account_good = false, user_good = false;
	int i;

	if (!resv_ptr) {
		info("Reservation name not found (%s)", job_ptr->resv_name);
		return ESLURM_RESERVATION_INVALID;
	}

	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT) {
		verbose("Job %u attempting to use reservation %s with floating "
			"start time", job_ptr->job_id, resv_ptr->name);
		return ESLURM_RESERVATION_ACCESS;
	}

	/* Determine if we have access */
	if (accounting_enforce & ACCOUNTING_ENFORCE_ASSOCS) {
		char tmp_char[30];
		slurmdb_association_rec_t *assoc;
		if (!resv_ptr->assoc_list) {
			error("Reservation %s has no association list. "
			      "Checking user/account lists",
			      resv_ptr->name);
			goto no_assocs;
		}

		if (!job_ptr->assoc_ptr) {
			slurmdb_association_rec_t assoc_rec;
			/* This should never be called, but just to be
			 * safe we will try to fill it in. */
			memset(&assoc_rec, 0,
			       sizeof(slurmdb_association_rec_t));
			assoc_rec.id = job_ptr->assoc_id;
			if (assoc_mgr_fill_in_assoc(
				    acct_db_conn, &assoc_rec,
				    accounting_enforce,
				    (slurmdb_association_rec_t **)
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
				if (strstr(resv_ptr->assoc_list, tmp_char))
					goto end_it;	/* explicitly denied */
				assoc = assoc->usage->parent_assoc_ptr;
			}
		}
		if (strstr(resv_ptr->assoc_list, ",1") ||
		    strstr(resv_ptr->assoc_list, ",2") ||
		    strstr(resv_ptr->assoc_list, ",3") ||
		    strstr(resv_ptr->assoc_list, ",4") ||
		    strstr(resv_ptr->assoc_list, ",5") ||
		    strstr(resv_ptr->assoc_list, ",6") ||
		    strstr(resv_ptr->assoc_list, ",7") ||
		    strstr(resv_ptr->assoc_list, ",8") ||
		    strstr(resv_ptr->assoc_list, ",9") ||
		    strstr(resv_ptr->assoc_list, ",0")) {
			assoc = job_ptr->assoc_ptr;
			while (assoc) {
				snprintf(tmp_char, sizeof(tmp_char), ",%u,",
					 assoc->id);
				if (strstr(resv_ptr->assoc_list, tmp_char))
					return SLURM_SUCCESS;
				assoc = assoc->usage->parent_assoc_ptr;
			}
		} else {
			return SLURM_SUCCESS;
		}
	} else {
no_assocs:	if ((resv_ptr->user_cnt == 0) || resv_ptr->user_not)
			user_good = true;
		for (i = 0; i < resv_ptr->user_cnt; i++) {
			if (job_ptr->user_id == resv_ptr->user_list[i]) {
				if (resv_ptr->user_not)
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

		if ((resv_ptr->account_cnt == 0) || resv_ptr->account_not)
			account_good = true;
		for (i=0; (i<resv_ptr->account_cnt) && job_ptr->account; i++) {
			if (resv_ptr->account_list[i] &&
			    (strcmp(job_ptr->account,
				    resv_ptr->account_list[i]) == 0)) {
				if (resv_ptr->account_not)
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
	info("Security violation, uid=%u account=%s attempt to use "
	     "reservation %s",
	     job_ptr->user_id, job_ptr->account, resv_ptr->name);
	return ESLURM_RESERVATION_ACCESS;
}

/*
 * Determine if a job can start now based only upon reservations
 *
 * IN job_ptr      - job to test
 * RET	SLURM_SUCCESS if runable now, otherwise an error code
 */
extern int job_test_resv_now(struct job_record *job_ptr)
{
	slurmctld_resv_t * resv_ptr;
	time_t now;
	int rc;

	if (job_ptr->resv_name == NULL)
		return SLURM_SUCCESS;

	resv_ptr = (slurmctld_resv_t *) list_find_first (resv_list,
			_find_resv_name, job_ptr->resv_name);
	job_ptr->resv_ptr = resv_ptr;
	rc = _valid_job_access_resv(job_ptr, resv_ptr);
	if (rc != SLURM_SUCCESS)
		return rc;

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
	    !(resv_ptr->flags & RESERVE_FLAG_LIC_ONLY)) {
		/* empty reservation treated like it will start later */
		return ESLURM_INVALID_TIME_VALUE;
	}

	return SLURM_SUCCESS;
}

/* Adjust a job's time_limit and end_time as needed to avoid using
 *	reserved resources. Don't go below job's time_min value. */
extern void job_time_adj_resv(struct job_record *job_ptr)
{
	ListIterator iter;
	slurmctld_resv_t * resv_ptr;
	time_t now = time(NULL);
	int32_t resv_begin_time;

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if (resv_ptr->end_time <= now)
			_advance_resv_time(resv_ptr);
		if (job_ptr->resv_ptr == resv_ptr)
			continue;	/* authorized user of reservation */
		if (resv_ptr->start_time <= now)
			continue;	/* already validated */
		if (resv_ptr->start_time >= job_ptr->end_time)
			continue;	/* reservation starts after job ends */
		if (!license_list_overlap(job_ptr->license_list,
					  resv_ptr->license_list) &&
		    ((resv_ptr->node_bitmap == NULL) ||
		     (bit_overlap(resv_ptr->node_bitmap,
				  job_ptr->node_bitmap) == 0)))
			continue;	/* disjoint resources */
		resv_begin_time = difftime(resv_ptr->start_time, now) / 60;
		job_ptr->time_limit = MIN(job_ptr->time_limit,resv_begin_time);
	}
	list_iterator_destroy(iter);
	job_ptr->time_limit = MAX(job_ptr->time_limit, job_ptr->time_min);
	job_end_time_reset(job_ptr);
}

/* For a given license_list, return the total count of licenses of the
 *	specified name */
static int _license_cnt(List license_list, char *lic_name)
{
	int lic_cnt = 0;
	ListIterator iter;
	licenses_t *license_ptr;

	if (license_list == NULL)
		return lic_cnt;

	iter = list_iterator_create(license_list);
	while ((license_ptr = list_next(iter))) {
		if (strcmp(license_ptr->name, lic_name) == 0)
			lic_cnt += license_ptr->total;
	}
	list_iterator_destroy(iter);

	return lic_cnt;
}

static uint32_t _get_job_duration(struct job_record *job_ptr)
{
	uint32_t duration;
	uint16_t time_slices = 1;

	if (job_ptr->time_limit == INFINITE)
		duration = ONE_YEAR;
	else if (job_ptr->time_limit != NO_VAL)
		duration = (job_ptr->time_limit * 60);
	else {	/* partition time limit */
		if (job_ptr->part_ptr->max_time == INFINITE)
			duration = ONE_YEAR;
		else
			duration = (job_ptr->part_ptr->max_time * 60);
	}
	if (job_ptr->part_ptr)
		time_slices = job_ptr->part_ptr->max_share & ~SHARED_FORCE;
	if ((duration != ONE_YEAR) && (time_slices > 1) &&
	    (slurm_get_preempt_mode() & PREEMPT_MODE_GANG)) {
		/* FIXME: Ideally we figure out how many jobs are actually
		 * time-slicing on each node rather than using the maximum
		 * value. */
		duration *= time_slices;
	}
	return duration;
}

/*
 * Determine how many licenses of the give type the specified job is
 *	prevented from using due to reservations
 *
 * IN job_ptr   - job to test
 * IN lic_name  - name of license
 * IN when      - when the job is expected to start
 * RET number of licenses of this type the job is prevented from using
 */
extern int job_test_lic_resv(struct job_record *job_ptr, char *lic_name,
			     time_t when)
{
	slurmctld_resv_t * resv_ptr;
	time_t job_start_time, job_end_time, now = time(NULL);
	ListIterator iter;
	int resv_cnt = 0;

	job_start_time = when;
	job_end_time   = when + _get_job_duration(job_ptr);
	iter = list_iterator_create(resv_list);
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if (resv_ptr->end_time <= now)
			_advance_resv_time(resv_ptr);
		if ((resv_ptr->start_time >= job_end_time) ||
		    (resv_ptr->end_time   <= job_start_time))
			continue;	/* reservation at different time */

		if (job_ptr->resv_name &&
		    (strcmp(job_ptr->resv_name, resv_ptr->name) == 0))
			continue;	/* job can use this reservation */

		resv_cnt += _license_cnt(resv_ptr->license_list, lic_name);
	}
	list_iterator_destroy(iter);

	/* info("job %u blocked from %d licenses of type %s",
	     job_ptr->job_id, resv_cnt, lic_name); */
	return resv_cnt;
}

/*
 * Determine which nodes a job can use based upon reservations
 * IN job_ptr      - job to test
 * IN/OUT when     - when we want the job to start (IN)
 *                   when the reservation is available (OUT)
 * IN move_time    - if true, then permit the start time to advance from
 *                   "when" as needed IF job has no reservervation
 * OUT node_bitmap - nodes which the job can use, caller must free unless error
 * OUT exc_core_bitmap - cores which the job can NOT use, caller must free
 *			 unless error
 * RET	SLURM_SUCCESS if runable now
 *	ESLURM_RESERVATION_ACCESS access to reservation denied
 *	ESLURM_RESERVATION_INVALID reservation invalid
 *	ESLURM_INVALID_TIME_VALUE reservation invalid at time "when"
 *	ESLURM_NODES_BUSY job has no reservation, but required nodes are
 *			  reserved
 */
extern int job_test_resv(struct job_record *job_ptr, time_t *when,
			 bool move_time, bitstr_t **node_bitmap,
			 bitstr_t **exc_core_bitmap, bool *resv_overlap)
{
	slurmctld_resv_t * resv_ptr, *res2_ptr;
	time_t job_start_time, job_end_time, lic_resv_time;
	time_t start_relative, end_relative;
	time_t now = time(NULL);
	ListIterator iter;
	int i, rc = SLURM_SUCCESS, rc2;

	job_start_time = *when;
	job_end_time   = *when + _get_job_duration(job_ptr);
	*node_bitmap = (bitstr_t *) NULL;

	if (job_ptr->resv_name) {
		resv_ptr = (slurmctld_resv_t *) list_find_first (resv_list,
				_find_resv_name, job_ptr->resv_name);
		job_ptr->resv_ptr = resv_ptr;
		rc2 = _valid_job_access_resv(job_ptr, resv_ptr);
		if (rc2 != SLURM_SUCCESS)
			return rc2;
		if (resv_ptr->end_time <= now)
			_advance_resv_time(resv_ptr);
		if (*when < resv_ptr->start_time) {
			/* reservation starts later */
			*when = resv_ptr->start_time;
			return ESLURM_INVALID_TIME_VALUE;
		}
		if ((resv_ptr->node_cnt == 0) &&
		    (!(resv_ptr->flags & RESERVE_FLAG_LIC_ONLY))) {
			/* empty reservation treated like it will start later */
			*when = now + 600;
			return ESLURM_INVALID_TIME_VALUE;
		}
		if (*when > resv_ptr->end_time) {
			/* reservation ended earlier */
			*when = resv_ptr->end_time;
			if ((now > resv_ptr->end_time) ||
			    ((job_ptr->details) &&
			     (job_ptr->details->begin_time >
			      resv_ptr->end_time)))
				job_ptr->priority = 0;	/* admin hold */
			return ESLURM_RESERVATION_INVALID;
		}
		if (job_ptr->details->req_node_bitmap &&
		    (!(resv_ptr->flags & RESERVE_FLAG_LIC_ONLY)) &&
		    !bit_super_set(job_ptr->details->req_node_bitmap,
				   resv_ptr->node_bitmap)) {
			return ESLURM_RESERVATION_INVALID;
		}
		if (resv_ptr->flags & RESERVE_FLAG_LIC_ONLY) {
			*node_bitmap = bit_alloc(node_record_count);
			bit_nset(*node_bitmap, 0, (node_record_count - 1));
		} else
			*node_bitmap = bit_copy(resv_ptr->node_bitmap);

		/* if there are any overlapping reservations, we need to
		 * prevent the job from using those nodes (e.g. MAINT nodes) */
		iter = list_iterator_create(resv_list);
		while ((res2_ptr = (slurmctld_resv_t *) list_next(iter))) {
			if ((resv_ptr->flags & RESERVE_FLAG_MAINT) ||
			    ((resv_ptr->flags & RESERVE_FLAG_OVERLAP) &&
			     !(res2_ptr->flags & RESERVE_FLAG_MAINT)) ||
			    (res2_ptr == resv_ptr) ||
			    (res2_ptr->node_bitmap == NULL) ||
			    (res2_ptr->start_time >= job_end_time) ||
			    (res2_ptr->end_time   <= job_start_time) ||
			    (!res2_ptr->full_nodes))
				continue;
			if (bit_overlap(*node_bitmap, res2_ptr->node_bitmap)) {
				*resv_overlap = true;
				bit_not(res2_ptr->node_bitmap);
				bit_and(*node_bitmap, res2_ptr->node_bitmap);
				bit_not(res2_ptr->node_bitmap);
			}
		}
		list_iterator_destroy(iter);

		if (slurmctld_conf.debug_flags & DEBUG_FLAG_RESERVATION) {
			char *nodes = bitmap2node_name(*node_bitmap);
			info("job_test_resv: job:%u reservation:%s nodes:%s",
			     job_ptr->job_id, job_ptr->resv_name, nodes);
			xfree(nodes);
		}

		/* if reservation is using just partial nodes, this returns
		 * coremap to exclude */
		if (resv_ptr->core_bitmap && exc_core_bitmap) {
			*exc_core_bitmap = bit_copy(resv_ptr->core_bitmap);
			bit_not(*exc_core_bitmap);
		}

		return SLURM_SUCCESS;
	}

	job_ptr->resv_ptr = NULL;	/* should be redundant */
	*node_bitmap = bit_alloc(node_record_count);
	bit_nset(*node_bitmap, 0, (node_record_count - 1));
	if (list_count(resv_list) == 0)
		return SLURM_SUCCESS;
#ifdef HAVE_BG
	/* Since on a bluegene we track cnodes instead of cpus do the
	   adjustment since accounting is expecting cpus here.
	*/
	if (!cpus_per_mp)
		(void)select_g_alter_node_cnt(
			SELECT_GET_MP_CPU_CNT, &cpus_per_mp);

	/* If the job is looking for whole mp blocks we need to tell
	 * the reservations about it so it sends the plugin the correct
	 * thing.
	 */
	if (job_ptr->details->max_cpus < cpus_per_mp)
		job_ptr->details->whole_node = 0;
	else
		job_ptr->details->whole_node = 1;
#endif

	/* Job has no reservation, try to find time when this can
	 * run and get it's required nodes (if any) */
	for (i = 0; ; i++) {
		lic_resv_time = (time_t) 0;

		iter = list_iterator_create(resv_list);
		while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
			if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT) {
				start_relative = resv_ptr->start_time + now;
				if (resv_ptr->duration == INFINITE)
					end_relative = start_relative+ONE_YEAR;
				else if (resv_ptr->duration &&
					 (resv_ptr->duration != NO_VAL)) {
					end_relative = start_relative +
						       resv_ptr->duration * 60;
				} else {
					end_relative = resv_ptr->end_time;
					if (start_relative > end_relative)
						start_relative = end_relative;
				}
			} else {
				if (resv_ptr->end_time <= now)
					_advance_resv_time(resv_ptr);
				start_relative = resv_ptr->start_time_first;
				end_relative = resv_ptr->end_time;
			}

			if ((resv_ptr->node_bitmap == NULL) ||
			    (start_relative >= job_end_time) ||
			    (end_relative   <= job_start_time))
				continue;
			if (job_ptr->details->req_node_bitmap &&
			    bit_overlap(job_ptr->details->req_node_bitmap,
					resv_ptr->node_bitmap) &&
			    ((resv_ptr->cpu_cnt == 0) ||
			     (job_ptr->details->whole_node))) {
				*when = resv_ptr->end_time;
				rc = ESLURM_NODES_BUSY;
				break;
			}
			/* FIXME: This only tracks when ANY licenses required
			 * by the job are freed by any reservation without
			 * counting them, so the results are not accurate. */
			if (license_list_overlap(job_ptr->license_list,
						 resv_ptr->license_list)) {
				if ((lic_resv_time == (time_t) 0) ||
				    (lic_resv_time > resv_ptr->end_time))
					lic_resv_time = resv_ptr->end_time;
			}

			if ((resv_ptr->full_nodes) ||
			    (job_ptr->details->whole_node)) {
#if _DEBUG
				info("reservation %s uses full nodes or job %u "
				     "will not share nodes",
				     resv_ptr->name, job_ptr->job_id);
#endif
				bit_not(resv_ptr->node_bitmap);
				bit_and(*node_bitmap, resv_ptr->node_bitmap);
				bit_not(resv_ptr->node_bitmap);
			} else {
#if _DEBUG
				info("job_test_resv: reservation %s uses "
				     "partial nodes", resv_ptr->name);
#endif
				if (*exc_core_bitmap == NULL) {
					*exc_core_bitmap =
						bit_copy(resv_ptr->core_bitmap);
				} else {
					bit_or(*exc_core_bitmap,
					       resv_ptr->core_bitmap);
				}
			}
		}
		list_iterator_destroy(iter);

		if ((rc == SLURM_SUCCESS) && move_time) {
			if (license_job_test(job_ptr, job_start_time)
			    == EAGAIN) {
				/* Need to postpone for licenses. Time returned
				 * is best case; first reservation with those
				 * licenses ends. */
				rc = ESLURM_NODES_BUSY;
				*when = lic_resv_time;
			}
		}
		if (rc == SLURM_SUCCESS)
			break;
		/* rc == ESLURM_NODES_BUSY here from above break */
		if (move_time && (i<10)) {  /* Retry for later start time */
			bit_nset(*node_bitmap, 0, (node_record_count - 1));
			rc = SLURM_SUCCESS;
			continue;
		}
		FREE_NULL_BITMAP(*node_bitmap);
		break;	/* Give up */
	}

	return rc;
}

/*
 * Determine the time of the first reservation to end after some time.
 * return zero of no reservation ends after that time.
 * IN start_time - look for reservations ending after this time
 * RET the reservation end time or zero of none found
 */
extern time_t find_resv_end(time_t start_time)
{
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;
	time_t end_time = 0;

	if (!resv_list)
		return end_time;

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if ((start_time < resv_ptr->start_time) ||
		    (start_time > resv_ptr->end_time))
			continue;
		if ((end_time == 0) || (resv_ptr->end_time < end_time))
			end_time = resv_ptr->end_time;
	}
	list_iterator_destroy(iter);
	return end_time;
}

/* Begin scan of all jobs for valid reservations */
extern void begin_job_resv_check(void)
{
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;
	slurm_ctl_conf_t *conf;

	if (!resv_list)
		return;

	conf = slurm_conf_lock();
	resv_over_run = conf->resv_over_run;
	slurm_conf_unlock();
	if (resv_over_run == (uint16_t) INFINITE)
		resv_over_run = ONE_YEAR;
	else
		resv_over_run *= 60;

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		resv_ptr->job_pend_cnt = 0;
		resv_ptr->job_run_cnt  = 0;
	}
	list_iterator_destroy(iter);
}

/* Test a particular job for valid reservation
 *
 * RET ESLURM_INVALID_TIME_VALUE if reservation is terminated
 *     SLURM_SUCCESS if reservation is still valid
 */
extern int job_resv_check(struct job_record *job_ptr)
{
	bool run_flag = false;

	if (!job_ptr->resv_name)
		return SLURM_SUCCESS;

	if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))
		run_flag = true;
	else if (IS_JOB_PENDING(job_ptr))
		run_flag = false;
	else
		return SLURM_SUCCESS;

	xassert(job_ptr->resv_ptr->magic == RESV_MAGIC);
	if (run_flag)
		job_ptr->resv_ptr->job_run_cnt++;
	else
		job_ptr->resv_ptr->job_pend_cnt++;

	if ((job_ptr->resv_ptr->end_time + resv_over_run) < time(NULL))
		return ESLURM_INVALID_TIME_VALUE;
	return SLURM_SUCCESS;
}

/* Advance a expired reservation's time stamps one day or one week
 * as appropriate. */
static void _advance_resv_time(slurmctld_resv_t *resv_ptr)
{
	int day_cnt = 0;
	char *interval = "";

	if (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT)
		return;		/* Not applicable */

	if (resv_ptr->flags & RESERVE_FLAG_DAILY) {
		day_cnt = 1;
		interval = "day";
	} else if (resv_ptr->flags & RESERVE_FLAG_WEEKLY) {
		day_cnt = 7;
		interval = "week";
	}

	if (day_cnt) {
		verbose("Advance reservation %s one %s", resv_ptr->name,
			interval);
		resv_ptr->start_time = resv_ptr->start_time_first;
		_advance_time(&resv_ptr->start_time, day_cnt);
		resv_ptr->start_time_prev = resv_ptr->start_time;
		resv_ptr->start_time_first = resv_ptr->start_time;
		_advance_time(&resv_ptr->end_time, day_cnt);
		_post_resv_create(resv_ptr);
		last_resv_update = time(NULL);
		schedule_resv_save();
	}
}

static void _free_script_arg(resv_thread_args_t *args)
{
	if (args) {
		xfree(args->script);
		xfree(args->resv_name);
		xfree(args);
	}
}

static void *_fork_script(void *x)
{
	resv_thread_args_t *args = (resv_thread_args_t *) x;
	char *argv[3], *envp[1];
	int status, wait_rc;
	pid_t cpid;

	argv[0] = args->script;
	argv[1] = args->resv_name;
	argv[2] = NULL;
	envp[0] = NULL;
	if ((cpid = fork()) < 0) {
		error("_fork_script fork error: %m");
		goto fini;
	}
	if (cpid == 0) {
#ifdef SETPGRP_TWO_ARGS
		setpgrp(0, 0);
#else
		setpgrp();
#endif
		execve(argv[0], argv, envp);
		exit(127);
	}

	while (1) {
		wait_rc = waitpid(cpid, &status, 0);
		if (wait_rc < 0) {
			if (errno == EINTR)
				continue;
			error("_fork_script waitpid error: %m");
			break;
		} else if (wait_rc > 0) {
			killpg(cpid, SIGKILL);	/* kill children too */
			break;
		}
	}
fini:	_free_script_arg(args);
	return NULL;
}

static void _run_script(char *script, slurmctld_resv_t *resv_ptr)
{
	int rc;
	resv_thread_args_t *args;
	pthread_t thread_id_prolog;
	pthread_attr_t thread_attr_prolog;

	if (!script || !script[0])
		return;
	if (access(script, X_OK) < 0) {
		error("Invalid ResvProlog or ResvEpilog(%s): %m", script);
		return;
	}

	slurm_attr_init(&thread_attr_prolog);
	pthread_attr_setdetachstate(&thread_attr_prolog,
				    PTHREAD_CREATE_DETACHED);
	args = xmalloc(sizeof(resv_thread_args_t));
	args->script    = xstrdup(script);
	args->resv_name = xstrdup(resv_ptr->name);
	while (1) {
		rc = pthread_create(&thread_id_prolog, &thread_attr_prolog,
				    _fork_script, (void *) args);
		if (rc != 0) {
			if (errno == EAGAIN)
				continue;
			error("pthread_create: %m");
		}
		break;
	}
	slurm_attr_destroy(&thread_attr_prolog);
	if (rc != 0)
		_free_script_arg(args);
}

/* Finish scan of all jobs for valid reservations
 *
 * Purge vestigial reservation records.
 * Advance daily or weekly reservations that are no longer
 *	being actively used.
 */
extern void fini_job_resv_check(void)
{
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;
	time_t now = time(NULL);

	if (!resv_list)
		return;

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if (!resv_ptr->run_prolog || !resv_ptr->run_epilog)
			continue;
		if ((resv_ptr->end_time >= now) ||
		    (resv_ptr->duration && (resv_ptr->duration != NO_VAL) &&
		     (resv_ptr->flags & RESERVE_FLAG_TIME_FLOAT))) {
			_validate_node_choice(resv_ptr);
			continue;
		}
		_advance_resv_time(resv_ptr);
		if ((resv_ptr->job_run_cnt    == 0) &&
		    (resv_ptr->flags_set_node == 0) &&
		    ((resv_ptr->flags & RESERVE_FLAG_DAILY ) == 0) &&
		    ((resv_ptr->flags & RESERVE_FLAG_WEEKLY) == 0)) {
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

/* send all reservations to accounting.  Only needed at
 * first registration
 */
extern int send_resvs_to_accounting(void)
{
	ListIterator itr = NULL;
	slurmctld_resv_t *resv_ptr;
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, READ_LOCK };

	if (!resv_list)
		return SLURM_SUCCESS;

	lock_slurmctld(node_write_lock);

	itr = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(itr)))
		_post_resv_create(resv_ptr);
	list_iterator_destroy(itr);

	unlock_slurmctld(node_write_lock);

	return SLURM_SUCCESS;
}

/* Set or clear NODE_STATE_MAINT for node_state as needed
 * IN reset_all - if true, then re-initialize all node information for all
 *	reservations, but do not run any prologs or epilogs or count started
 *	reservations
 * RET count of newly started reservations
 */
extern int set_node_maint_mode(bool reset_all)
{
	int res_start_cnt = 0;
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;
	time_t now = time(NULL);

	if (!resv_list)
		return res_start_cnt;

	if (reset_all) {
		int i;
		struct node_record *node_ptr;
		uint32_t flags = (NODE_STATE_RES | NODE_STATE_MAINT);

		for (i = 0, node_ptr = node_record_table_ptr;
		     i <= node_record_count;
		     i++, node_ptr++) {
			node_ptr->node_state &= (~flags);
		}
	}
	iter = list_iterator_create(resv_list);
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		uint32_t flags = NODE_STATE_RES;
		if (reset_all)
			resv_ptr->flags_set_node = false;
		if (resv_ptr->flags & RESERVE_FLAG_MAINT)
			flags |= NODE_STATE_MAINT;

		if ((now >= resv_ptr->start_time) &&
		    (now <  resv_ptr->end_time  )) {
			if (!resv_ptr->flags_set_node) {
				resv_ptr->flags_set_node = true;
				_set_nodes_flags(resv_ptr, now, flags);
				last_node_update = now;
			}
		} else if (resv_ptr->flags_set_node) {
			resv_ptr->flags_set_node = false;
			_set_nodes_flags(resv_ptr, now, flags);
			last_node_update = now;
		}

		if (reset_all)	/* Defer reservation prolog/epilog */
			continue;
		if ((resv_ptr->start_time <= now) && !resv_ptr->run_prolog) {
			res_start_cnt++;
			resv_ptr->run_prolog = true;
			_run_script(slurmctld_conf.resv_prolog, resv_ptr);
		}
		if ((resv_ptr->end_time <= now) && !resv_ptr->run_epilog) {
			resv_ptr->run_epilog = true;
			_run_script(slurmctld_conf.resv_epilog, resv_ptr);
		}
	}
	list_iterator_destroy(iter);

	return res_start_cnt;
}

/* checks if node within node_record_table_ptr is in maint reservation */
extern bool is_node_in_maint_reservation(int nodenum)
{
	bool res = false;
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;
	time_t t;

	if (nodenum < 0 || nodenum >= node_record_count || !resv_list)
		return false;

	t = time(NULL);
	iter = list_iterator_create(resv_list);
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if ((resv_ptr->flags & RESERVE_FLAG_MAINT) == 0)
			continue;
		if (! (t >= resv_ptr->start_time
		       && t <= resv_ptr->end_time))
			continue;
		if (bit_test(resv_ptr->node_bitmap, nodenum)) {
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
	ListIterator  iter = NULL;
	slurmctld_lock_t node_write_lock = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, READ_LOCK };

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

extern void update_part_nodes_in_resv(struct part_record *part_ptr)
{
	ListIterator iter = NULL;
	struct part_record *parti_ptr = NULL;
	slurmctld_resv_t *resv_ptr = NULL;
	xassert(part_ptr);

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if ((resv_ptr->flags & RESERVE_FLAG_PART_NODES) &&
		    (resv_ptr->partition != NULL) &&
		    (strcmp(resv_ptr->partition, part_ptr->name) == 0)) {
			parti_ptr = find_part_record(resv_ptr->partition);
			FREE_NULL_BITMAP(resv_ptr->node_bitmap);
			resv_ptr->node_bitmap = bit_copy(parti_ptr->
							 node_bitmap);
			resv_ptr->node_cnt = bit_set_count(resv_ptr->
							   node_bitmap);
			xfree(resv_ptr->node_list);
			resv_ptr->node_list = xstrdup(parti_ptr->nodes);
			_set_cpu_cnt(resv_ptr);
			last_resv_update = time(NULL);
		}
	}
	list_iterator_destroy(iter);
}

static void _set_nodes_flags(slurmctld_resv_t *resv_ptr, time_t now,
			     uint32_t flags)
{
	int i, i_first, i_last;
	struct node_record *node_ptr;

	if (!resv_ptr->node_bitmap) {
		error("%s: reservation %s lacks a bitmap",
		      __func__, resv_ptr->name);
		return;
	}

	i_first = bit_ffs(resv_ptr->node_bitmap);
	if (i_first < 0) {
		if ((resv_ptr->flags & RESERVE_FLAG_LIC_ONLY) == 0) {
			error("%s: reservation %s includes no nodes",
			      __func__, resv_ptr->name);
		}
		return;
	}
	i_last  = bit_fls(resv_ptr->node_bitmap);
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(resv_ptr->node_bitmap, i))
			continue;

		node_ptr = node_record_table_ptr + i;
		if (resv_ptr->flags_set_node)
			node_ptr->node_state |= flags;
		else
			node_ptr->node_state &= (~flags);
		/* mark that this node is now down and in maint mode
		 * or was removed from maint mode */
		if (IS_NODE_DOWN(node_ptr) || IS_NODE_DRAIN(node_ptr) ||
		    IS_NODE_FAIL(node_ptr)) {
			clusteracct_storage_g_node_down(
				acct_db_conn,
				node_ptr, now, NULL,
				slurm_get_slurm_user_id());
		}
	}
}
