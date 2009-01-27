/*****************************************************************************\
 *  reservation.c - resource reservation management
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_time.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"

#define _RESV_DEBUG	0
#define RESV_MAGIC	0x3b82

/* Change RESV_STATE_VERSION value when changing the state save format */
#define RESV_STATE_VERSION      "VER001"

time_t last_resv_update = (time_t) 0;

List     resv_list = (List) NULL;
uint32_t top_suffix = 0;

static int  _build_account_list(char *accounts, int *account_cnt, 
			        char ***account_list);
static int  _build_uid_list(char *users, int *user_cnt, uid_t **user_list);
static void _del_resv_rec(void *x);
static void _deleted_resv(struct slurmctld_resv *resv_ptr);
static void _dump_resv_req(reserve_request_msg_t *resv_ptr, char *mode);
static int  _find_resv_name(void *x, void *key);
static void _generate_resv_name(reserve_request_msg_t *resv_ptr);
static bool _is_account_valid(char *account);
static bool _is_resv_used(slurmctld_resv_t *resv_ptr);
static void _pack_resv(struct slurmctld_resv *resv_ptr, Buf buffer,
		       bool internal);
static bool _resv_overlap(time_t start_time, time_t end_time, 
			  bitstr_t *node_bitmap,
			  struct slurmctld_resv *this_resv_ptr);
static void _set_assoc_list(struct slurmctld_resv *resv_ptr);
static void _set_cpu_cnt(struct slurmctld_resv *resv_ptr);
static void _set_resv_id(struct slurmctld_resv *resv_ptr);
static int  _update_account_list(struct slurmctld_resv *resv_ptr, 
				 char *accounts);
static int  _update_uid_list(struct slurmctld_resv *resv_ptr, char *users);
static void _updated_resv(struct slurmctld_resv *resv_ptr);
static void _validate_all_reservations(void);
static int  _valid_job_access_resv(struct job_record *job_ptr,
				   slurmctld_resv_t *resv_ptr);
static bool _validate_one_reservation(slurmctld_resv_t *resv_ptr);


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
		xfree(resv_ptr->features);
		xfree(resv_ptr->name);
		if (resv_ptr->node_bitmap)
			bit_free(resv_ptr->node_bitmap);
		xfree(resv_ptr->node_list);
		xfree(resv_ptr->partition);
		xfree(resv_ptr->users);
		xfree(resv_ptr->user_list);
		xfree(resv_ptr);
	}
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

static void _dump_resv_req(reserve_request_msg_t *resv_ptr, char *mode)
{
#ifdef _RESV_DEBUG
	char start_str[32] = "", end_str[32] = "", *type_str;
	int duration;

	slurm_make_time_str(&resv_ptr->start_time,start_str,sizeof(start_str));
	slurm_make_time_str(&resv_ptr->end_time,  end_str,  sizeof(end_str));
	if (resv_ptr->type == RESERVE_TYPE_MAINT)
		type_str = "MAINT";
	else
		type_str = "";
	if (resv_ptr->duration == NO_VAL)
		duration = -1;
	else
		duration = resv_ptr->duration;

	info("%s: Name=%s StartTime=%s EndTime=%s Duration=%d "
	     "Type=%s NodeCnt=%u NodeList=%s Features=%s "
	     "PartitionName=%s Users=%s Accounts=%s",
	     mode, resv_ptr->name, start_str, end_str, duration,
	     type_str, resv_ptr->node_cnt, resv_ptr->node_list, 
	     resv_ptr->features, resv_ptr->partition, 
	     resv_ptr->users, resv_ptr->accounts);
#endif
}

static void _generate_resv_name(reserve_request_msg_t *resv_ptr)
{
	char *key, *name, *sep, tmp[32];
	int len;

	/* Generate name prefix, based upon the first account
	 * name if provided otherwise first user name */
	if (resv_ptr->accounts && resv_ptr->accounts[0])
		key = resv_ptr->accounts;
	else
		key = resv_ptr->users;
	sep = strchr(key, ',');
	if (sep)
		len = sep - key;
	else
		len = strlen(key);
	name = xmalloc(len + 16);
	strncpy(name, key, len);
	strcat(name, "_");
	len++;

	if (top_suffix > 0xffffff00)
		top_suffix = 0;	/* Wrap around */
	snprintf(tmp, sizeof(tmp), "%d", ++top_suffix);
	strcat(name, tmp);
	resv_ptr->name = name;
}

/* Validate an account name */
static bool _is_account_valid(char *account)
{
	/* FIXME: Need to add logic here */
	return true;
}

/* Set a association list based upon accounts and users */
static void _set_assoc_list(struct slurmctld_resv *resv_ptr)
{
	/* FIXME: Need to add logic here to get assoc_list from slurmdbd
	 * based upon resv_ptr->account_cnt and account_list plus
	 * user_cnt and user_list */
	xfree(resv_ptr->assoc_list);	/* clear for modify */
	resv_ptr->assoc_list = xstrdup("TBD");	/* FOR TESTING ONLY */
}

/* Set a unique reservation id */
static void _set_resv_id(struct slurmctld_resv *resv_ptr)
{
	/* FIXME: Need to add logic here to get resv_id from slurmdbd */
	/* NOTE: resv_id is zero on entry */
	resv_ptr->resv_id = top_suffix;	/* FOR TESTING ONLY */
}

/* Note that a reservation has been deleted */
static void _deleted_resv(struct slurmctld_resv *resv_ptr)
{
	/* FIXME: Need to add logic here to notify slurmdbd */
}

/* Note that a reservation has been updated */
static void _updated_resv(struct slurmctld_resv *resv_ptr)
{
	/* FIXME: Need to add logic here to notify slurmdbd */
}

/*
 * Validate a comma delimited list of account names and build an array of
 *	them
 * IN account       - a list of account names
 * OUT account_cnt  - number of accounts in the list
 * OUT account_list - list of the account names, 
 *		      CALLER MUST XFREE this plus each individual record
 * RETURN 0 on success
 */
static int _build_account_list(char *accounts, int *account_cnt, 
			       char ***account_list)
{
	char *last = NULL, *tmp, *tok;
	int ac_cnt = 0, i;
	char **ac_list;

	*account_cnt = 0;
	*account_list = (char **) NULL;

	if (!accounts)
		return ESLURM_INVALID_BANK_ACCOUNT;

	i = strlen(accounts);
	ac_list = xmalloc(sizeof(char *) * (i + 2));
	tmp = xstrdup(accounts);
	tok = strtok_r(tmp, ",", &last);
	while (tok) {
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
	return ESLURM_INVALID_BANK_ACCOUNT;
}

/*
 * Update a account list for an existing reservation based upon an 
 *	update comma delimited specification of accounts to add (+name), 
 *	remove (-name), or set value of
 * IN/OUT resv_ptr - pointer to reservation structure being updated
 * IN accounts     - a list of account names, to set, add, or remove
 * RETURN 0 on success
 */
static int  _update_account_list(struct slurmctld_resv *resv_ptr, 
				 char *accounts)
{
	char *last = NULL, *tmp, *tok;
	int ac_cnt = 0, i, j, k;
	int *ac_type, minus_account = 0, plus_account = 0;
	char **ac_list;
	bool found_it;

	if (!accounts)
		return ESLURM_INVALID_BANK_ACCOUNT;

	i = strlen(accounts);
	ac_list = xmalloc(sizeof(char *) * (i + 2));
	ac_type = xmalloc(sizeof(int)    * (i + 2));
	tmp = xstrdup(accounts);
	tok = strtok_r(tmp, ",", &last);
	while (tok) {
		if (tok[0] == '-') {
			ac_type[ac_cnt] = 1;	/* minus */
			minus_account = 1;
			tok++;
		} else if (tok[0] == '+') {
			ac_type[ac_cnt] = 2;	/* plus */
			plus_account = 1;
			tok++;
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
	xfree(tmp);

	if ((plus_account == 0) && (minus_account == 0)) {
		/* Just a reset of account list */
		xfree(resv_ptr->accounts);
		resv_ptr->accounts = xstrdup(accounts);
		xfree(resv_ptr->account_list);
		resv_ptr->account_list = ac_list;
		resv_ptr->account_cnt = ac_cnt;
		xfree(ac_type);
		return SLURM_SUCCESS;
	}

	/* Modification of existing account list */
	if (minus_account) {
		if (resv_ptr->account_cnt == 0)
			goto inval;
		for (i=0; i<ac_cnt; i++) {
			if (ac_type[i] != 1)
				continue;
			found_it = false;
			for (j=0; j<resv_ptr->account_cnt; j++) {
				if (strcmp(resv_ptr->account_list[j], 
					   ac_list[i])) {
					continue;
				}
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
			if (i == 0) {
				resv_ptr->accounts = xstrdup(resv_ptr->
							     account_list[i]);
			} else {
				xstrcat(resv_ptr->accounts, ",");
				xstrcat(resv_ptr->accounts,
					resv_ptr->account_list[i]);
			}
		}
	}

	if (plus_account) {
		for (i=0; i<ac_cnt; i++) {
			if (ac_type[i] != 2)
				continue;
			found_it = false;
			for (j=0; j<resv_ptr->account_cnt; j++) {
				if (strcmp(resv_ptr->account_list[j], 
					   ac_list[i])) {
					continue;
				}
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
			if (i == 0) {
				resv_ptr->accounts = xstrdup(resv_ptr->
							     account_list[i]);
			} else {
				xstrcat(resv_ptr->accounts, ",");
				xstrcat(resv_ptr->accounts,
					resv_ptr->account_list[i]);
			}
		}
	}
	for (i=0; i<ac_cnt; i++)
		xfree(ac_list[i]);
	xfree(ac_list);
	xfree(ac_type);
	return SLURM_SUCCESS;

 inval:	for (i=0; i<ac_cnt; i++)
		xfree(ac_list[i]);
	xfree(ac_list);
	xfree(ac_type);
	xfree(tmp);
	return ESLURM_INVALID_BANK_ACCOUNT;
}

/*
 * Validate a comma delimited list of user names and build an array of
 *	their UIDs
 * IN users      - a list of user names
 * OUT user_cnt  - number of UIDs in the list
 * OUT user_list - list of the user's uid, CALLER MUST XFREE;
 * RETURN 0 on success
 */
static int _build_uid_list(char *users, int *user_cnt, uid_t **user_list)
{
	char *last = NULL, *tmp = NULL, *tok;
	int u_cnt = 0, i;
	uid_t *u_list, u_tmp;

	*user_cnt = 0;
	*user_list = (uid_t *) NULL;

	if (!users)
		return ESLURM_USER_ID_MISSING;

	i = strlen(users);
	u_list = xmalloc(sizeof(uid_t) * (i + 2));
	tmp = xstrdup(users);
	tok = strtok_r(tmp, ",", &last);
	while (tok) {
		u_tmp = uid_from_string(tok);
		if (u_tmp == (uid_t) -1) {
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
static int _update_uid_list(struct slurmctld_resv *resv_ptr, char *users)
{
	char *last = NULL, *tmp = NULL, *tok;
	int u_cnt = 0, i, j, k;
	uid_t *u_list, u_tmp;
	int *u_type, minus_user = 0, plus_user = 0;
	char **u_name;
	bool found_it;

	if (!users)
		return ESLURM_USER_ID_MISSING;

	/* Parse the incoming user expression */
	i = strlen(users);
	u_list = xmalloc(sizeof(uid_t)  * (i + 2));
	u_name = xmalloc(sizeof(char *) * (i + 2));
	u_type = xmalloc(sizeof(int)    * (i + 2));
	tmp = xstrdup(users);
	tok = strtok_r(tmp, ",", &last);
	while (tok) {
		if (tok[0] == '-') {
			u_type[u_cnt] = 1;	/* minus */
			minus_user = 1;
			tok++;
		} else if (tok[0] == '+') {
			u_type[u_cnt] = 2;	/* plus */
			plus_user = 1;
			tok++;
		} else if (plus_user || minus_user) {
			info("Reservation user expression invalid %s", users);
			goto inval;
		} else
			u_type[u_cnt] = 3;	/* set */
		u_tmp = uid_from_string(tok);
		if (u_tmp == (uid_t) -1) {
			info("Reservation request has invalid user %s", tok);
			goto inval;
		}
		u_name[u_cnt] = tok;
		u_list[u_cnt++] = u_tmp;
		tok = strtok_r(NULL, ",", &last);
	}
	xfree(tmp);

	if ((plus_user == 0) && (minus_user == 0)) {
		/* Just a reset of user list */
		xfree(resv_ptr->users);
		xfree(resv_ptr->user_list);
		resv_ptr->users = xstrdup(users);
		resv_ptr->user_cnt  = u_cnt;
		resv_ptr->user_list = u_list;
		xfree(u_name);
		xfree(u_type);
		return SLURM_SUCCESS;
	}
	
	/* Modification of existing user list */
	if (minus_user) {
		for (i=0; i<u_cnt; i++) {
			if (u_type[i] != 1)
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
				     (tok[-1] != ',')) ||
				    ((tok[k] != '\0') && (tok[k] != ','))) {
					tmp = tok + 1;
					continue;
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
	}

	if (plus_user) {
		for (i=0; i<u_cnt; i++) {
			if (u_type[i] != 2)
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
			xstrcat(resv_ptr->users, u_name[i]);
			xrealloc(resv_ptr->user_list, 
				 sizeof(uid_t) * (resv_ptr->user_cnt + 1));
			resv_ptr->user_list[resv_ptr->user_cnt++] =
				u_list[i];
		}
	}
	xfree(u_list);
	xfree(u_name);
	xfree(u_type);
	return SLURM_SUCCESS;

 inval:	xfree(tmp);
	xfree(u_list);
	xfree(u_name);
	xfree(u_type);
	return ESLURM_USER_ID_MISSING;
}

/* 
 * _pack_resv - dump all configuration information about a specific reservation
 *	in machine independent form (for network transmission)
 * IN resv_ptr - pointer to reservation for which information is requested
 * IN/OUT buffer - buffer in which data is placed, pointers automatically 
 *	updated
 * IN internal   - true if for internal save state, false for xmit to users
 * NOTE: if you make any changes here be sure to make the corresponding 
 *	to _unpack_reserve_info_members() in common/slurm_protocol_pack.c
 *	plus load_all_resv_state() below.
 */
static void _pack_resv(struct slurmctld_resv *resv_ptr, Buf buffer, 
		       bool internal)
{
	packstr(resv_ptr->accounts,	buffer);
	pack_time(resv_ptr->end_time,	buffer);
	packstr(resv_ptr->features,	buffer);
	packstr(resv_ptr->name,		buffer);
	pack32(resv_ptr->node_cnt,	buffer);
	packstr(resv_ptr->node_list,	buffer);
	packstr(resv_ptr->partition,	buffer);
	pack_time(resv_ptr->start_time,	buffer);
	pack16(resv_ptr->type,		buffer);
	packstr(resv_ptr->users,	buffer);

	if (internal) {
		packstr(resv_ptr->assoc_list,	buffer);
		pack32(resv_ptr->cpu_cnt,	buffer);
		pack32(resv_ptr->resv_id,	buffer);
	}
}

/*
 * Test if a new/updated reservation request overlaps an existing
 *	reservation
 * RET true if overlap
 */
static bool _resv_overlap(time_t start_time, time_t end_time, 
			  bitstr_t *node_bitmap,
			  struct slurmctld_resv *this_resv_ptr)
{
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;
	bool rc = false;

	if (!node_bitmap)
		return rc;

	iter = list_iterator_create(resv_list);
	if (!iter)
		fatal("malloc: list_iterator_create");
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {\
		if (resv_ptr == this_resv_ptr)
			continue;	/* skip self */
		if ((resv_ptr->end_time   <= start_time) ||
		    (resv_ptr->start_time >= end_time) ||
		    (resv_ptr->node_bitmap == NULL) ||
		    (bit_overlap(resv_ptr->node_bitmap, node_bitmap) == 0))
			continue;
		verbose("Reservation overlap with %s", resv_ptr->name);
		rc = true;
		break;
	}
	list_iterator_destroy(iter);

	return rc;
}

/* Set a reservation's CPU count. Requires that the reservation's
 *	node_bitmap be set. */
static void _set_cpu_cnt(struct slurmctld_resv *resv_ptr)
{
	int i;
	uint32_t cpu_cnt = 0;
	struct node_record *node_ptr = node_record_table_ptr;

	if (!resv_ptr->node_bitmap)
		return;

	for (i=0; i<node_record_count; i++, node_ptr++) {
		if (!bit_test(resv_ptr->node_bitmap, i))
			continue;
		if (slurmctld_conf.fast_schedule)
			cpu_cnt += node_ptr->config_ptr->cpus;
		else
			cpu_cnt += node_ptr->cpus;
	}
	resv_ptr->cpu_cnt = cpu_cnt;
}

/* Create a resource reservation */
extern int create_resv(reserve_request_msg_t *resv_desc_ptr)
{
	int i, rc = SLURM_SUCCESS;
	time_t now = time(NULL);
	struct part_record *part_ptr = NULL;
	bitstr_t *node_bitmap = NULL;
	slurmctld_resv_t *resv_ptr;
	int account_cnt = 0, user_cnt = 0;
	char **account_list = NULL;
	uid_t *user_list = NULL;

	if (!resv_list)
		resv_list = list_create(_del_resv_rec);
	_dump_resv_req(resv_desc_ptr, "create_resv");

	/* Validate the request */
	if (resv_desc_ptr->start_time != (time_t) NO_VAL) {
		if (resv_desc_ptr->start_time < (now - 60)) {
			info("Reservation requestion has invalid start time");
			rc = ESLURM_INVALID_TIME_VALUE;
			goto bad_parse;
		}
	} else
		resv_desc_ptr->start_time = now;
	if (resv_desc_ptr->end_time != (time_t) NO_VAL) {
		if (resv_desc_ptr->end_time < (now - 60)) {
			info("Reservation requestion has invalid end time");
			rc = ESLURM_INVALID_TIME_VALUE;
			goto bad_parse;
		}
	} else if (resv_desc_ptr->duration) {
		resv_desc_ptr->end_time = resv_desc_ptr->start_time +
					  (resv_desc_ptr->duration * 60);
	} else
		resv_desc_ptr->end_time = INFINITE;
	if (resv_desc_ptr->type == (uint16_t) NO_VAL)
		resv_desc_ptr->type = 0;
	else if (resv_desc_ptr->type > RESERVE_TYPE_MAINT) {
		info("Invalid reservation type %u ignored",
		      resv_desc_ptr->type);
		resv_desc_ptr->type = 0;
	}
	if (resv_desc_ptr->partition) {
		part_ptr = find_part_record(resv_desc_ptr->partition);
		if (!part_ptr) {
			info("Reservation request has invalid partition %s",
			     resv_desc_ptr->partition);
			rc = ESLURM_INVALID_PARTITION_NAME;
			goto bad_parse;
		}
	}
	if ((resv_desc_ptr->accounts == NULL) &&
	    (resv_desc_ptr->users == NULL)) {
		info("Reservation request lacks users or accounts");
		rc = ESLURM_INVALID_BANK_ACCOUNT;
		goto bad_parse;
	}
	if (resv_desc_ptr->accounts) {
		rc = _build_account_list(resv_desc_ptr->accounts, 
					 &account_cnt, &account_list);
		if (rc)
			goto bad_parse;
	}
	if (resv_desc_ptr->users) {
		rc = _build_uid_list(resv_desc_ptr->users, 
				     &user_cnt, &user_list);
		if (rc)
			goto bad_parse;
	}
	if (resv_desc_ptr->name) {
		resv_ptr = (slurmctld_resv_t *) list_find_first (resv_list, 
				_find_resv_name, resv_desc_ptr->name);
		if (resv_ptr) {
			info("Reservation requestion name duplication (%s)",
			     resv_desc_ptr->name);
			rc = ESLURM_RESERVATION_INVALID;
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
			/* Same as explicitly created name, retry */
		}
	}
	if (resv_desc_ptr->node_list) {
		if (strcmp(resv_desc_ptr->node_list, "ALL") == 0) {
			node_bitmap = bit_alloc(node_record_count);
			bit_nset(node_bitmap, 0, (node_record_count - 1));
		} else if (node_name2bitmap(resv_desc_ptr->node_list, 
					    false, &node_bitmap)) {
			rc = ESLURM_INVALID_NODE_NAME;
			goto bad_parse;
		}
		if (resv_desc_ptr->node_cnt == NO_VAL)
			resv_desc_ptr->node_cnt = 0;
		if (_resv_overlap(resv_desc_ptr->start_time, 
				  resv_desc_ptr->end_time, node_bitmap,
				  NULL)) {
			info("Reservation requestion overlaps another");
			rc = ESLURM_INVALID_TIME_VALUE;
			goto bad_parse;
		}
	} else if (resv_desc_ptr->node_cnt == NO_VAL) {
		info("Reservation request lacks node specification");
		rc = ESLURM_INVALID_NODE_NAME;
		goto bad_parse;
	}

	/* Create a new reservation record */
	resv_ptr = xmalloc(sizeof(slurmctld_resv_t));
	resv_ptr->accounts	= resv_desc_ptr->accounts;
	resv_desc_ptr->accounts = NULL;		/* Nothing left to free */
	resv_ptr->account_cnt	= account_cnt;
	resv_ptr->account_list	= account_list;
	resv_ptr->end_time	= resv_desc_ptr->end_time;
	resv_ptr->features	= resv_desc_ptr->features;
	resv_desc_ptr->features = NULL;		/* Nothing left to free */
	xassert(resv_ptr->magic = RESV_MAGIC);	/* Sets value */
	resv_ptr->name		= xstrdup(resv_desc_ptr->name);
	resv_ptr->node_cnt	= resv_desc_ptr->node_cnt;
	resv_ptr->node_list	= resv_desc_ptr->node_list;
	resv_desc_ptr->node_list = NULL;	/* Nothing left to free */
	resv_ptr->node_bitmap	= node_bitmap;	/* May be unset */
	resv_ptr->partition	= resv_desc_ptr->partition;
	resv_desc_ptr->partition = NULL;	/* Nothing left to free */
	resv_ptr->part_ptr	= part_ptr;
	resv_ptr->start_time	= resv_desc_ptr->start_time;
	resv_ptr->type		= resv_desc_ptr->type;
	resv_ptr->users		= resv_desc_ptr->users;
	resv_ptr->user_cnt	= user_cnt;
	resv_ptr->user_list	= user_list;
	resv_desc_ptr->users 	= NULL;		/* Nothing left to free */
	_set_resv_id(resv_ptr);
	_set_cpu_cnt(resv_ptr);
	_set_assoc_list(resv_ptr);

	info("Created reservation %s for accounts=%s users=%s",
	     resv_ptr->name, resv_ptr->accounts, resv_ptr->users);
	list_append(resv_list, resv_ptr);
	last_resv_update = now;
	schedule_resv_save();

	return SLURM_SUCCESS;

 bad_parse:
	for (i=0; i<account_cnt; i++)
		xfree(account_list[i]);
	xfree(account_list);
	if (node_bitmap)
		bit_free(node_bitmap);
	xfree(user_list);
	return rc;
}

/* Update an exiting resource reservation */
extern int update_resv(reserve_request_msg_t *resv_desc_ptr)
{
	time_t now = time(NULL);
	time_t old_start_time, old_end_time;
	bitstr_t *old_node_bitmap = (bitstr_t *) NULL;
	char *old_node_list = NULL;
	slurmctld_resv_t *resv_ptr;
	int error_code = SLURM_SUCCESS;

	if (!resv_list)
		resv_list = list_create(_del_resv_rec);
	_dump_resv_req(resv_desc_ptr, "update_resv");

	/* Find the specified reservation */
	if ((resv_desc_ptr->name == NULL))
		return ESLURM_RESERVATION_INVALID;
	resv_ptr = (slurmctld_resv_t *) list_find_first (resv_list, 
			_find_resv_name, resv_desc_ptr->name);
	if (!resv_ptr)
		return ESLURM_RESERVATION_INVALID;

	/* Process the request */
	last_resv_update = now;
	if (resv_desc_ptr->type != (uint16_t) NO_VAL) {
		if (resv_desc_ptr->type > RESERVE_TYPE_MAINT) {
			error("Invalid reservation type %u ignored",
			      resv_desc_ptr->type);
		} else
			resv_ptr->type = resv_desc_ptr->type;
	}
	if (resv_desc_ptr->partition) {
		struct part_record *part_ptr = NULL;
		part_ptr = find_part_record(resv_desc_ptr->partition);
		if (!part_ptr) {
			info("Reservation request has invalid partition %s",
			     resv_desc_ptr->partition);
			error_code = ESLURM_INVALID_PARTITION_NAME;
			goto fini;
		}
		resv_ptr->partition	= resv_desc_ptr->partition;
		resv_desc_ptr->partition = NULL; /* Nothing left to free */
		resv_ptr->part_ptr	= part_ptr;
	}
	if (resv_desc_ptr->node_cnt != NO_VAL)
		resv_ptr->node_cnt = resv_desc_ptr->node_cnt;
	if (resv_desc_ptr->accounts) {
		int rc;
		rc = _update_account_list(resv_ptr, resv_desc_ptr->accounts);
		if (rc) {
			error_code = rc;
			goto fini;
		}
	}
	if (resv_desc_ptr->features) {
		xfree(resv_ptr->features);
		resv_ptr->features = resv_desc_ptr->features;
		resv_desc_ptr->features = NULL;	/* Nothing left to free */
	}
	if (resv_desc_ptr->users) {
		int rc;
		rc = _update_uid_list(resv_ptr, 
				      resv_desc_ptr->users);
		if (rc) {
			error_code = rc;
			goto fini;
		}
	}

	old_start_time = resv_ptr->start_time;
	old_end_time   = resv_ptr->end_time;
	if (resv_desc_ptr->start_time != (time_t) NO_VAL) {
		if (resv_desc_ptr->start_time < (now - 60)) {
			info("Reservation requestion has invalid start time");
			error_code = ESLURM_INVALID_TIME_VALUE;
			goto fini;
		}
		resv_ptr->start_time = resv_desc_ptr->start_time;
	}
	if (resv_desc_ptr->end_time != (time_t) NO_VAL) {
		if (resv_desc_ptr->end_time < (now - 60)) {
			info("Reservation requestion has invalid end time");
			error_code = ESLURM_INVALID_TIME_VALUE;
			goto fini;
		}
		resv_ptr->end_time = resv_desc_ptr->end_time;
	}
	if (resv_desc_ptr->duration != NO_VAL) {
		resv_ptr->end_time = resv_ptr->start_time + 
				     (resv_desc_ptr->duration * 60);
	}
	if (resv_desc_ptr->node_list) {		/* Change bitmap last */
		bitstr_t *node_bitmap;
		if (strcmp(resv_desc_ptr->node_list, "ALL") == 0) {
			node_bitmap = bit_alloc(node_record_count);
			bit_nset(node_bitmap, 0, (node_record_count - 1));
		} else if (node_name2bitmap(resv_desc_ptr->node_list, 
					    false, &node_bitmap)) {
			error_code = ESLURM_INVALID_NODE_NAME;
			/* Restore state with respect to time and nodes */
			resv_ptr->start_time = old_start_time;
			resv_ptr->end_time   = old_start_time;
			goto fini;
		}
		old_node_list = resv_ptr->node_list;
		resv_ptr->node_list = resv_desc_ptr->node_list;
		resv_desc_ptr->node_list = NULL;  /* Nothing left to free */
		old_node_bitmap = resv_ptr->node_bitmap;
		resv_ptr->node_bitmap = node_bitmap;
	}
	if (_resv_overlap(resv_ptr->start_time, resv_ptr->end_time, 
			  resv_ptr->node_bitmap, resv_ptr)) {
		info("Reservation requestion overlaps another");
		error_code = ESLURM_INVALID_TIME_VALUE;
		/* Restore state with respect to time and nodes */
		resv_ptr->start_time = old_start_time;
		resv_ptr->end_time   = old_end_time;
		if (old_node_list) {
			xfree(resv_ptr->node_list);
			resv_ptr->node_list = old_node_list;
			FREE_NULL_BITMAP(resv_ptr->node_bitmap);
			resv_ptr->node_bitmap = old_node_bitmap;
		}
	} else if (old_node_list) {
		_set_cpu_cnt(resv_ptr);
		/* Free temporarily saved state */
		xfree(old_node_list);
		FREE_NULL_BITMAP(old_node_bitmap);
	}

fini:	last_resv_update = now;
	_set_assoc_list(resv_ptr);
	_updated_resv(resv_ptr);
	schedule_resv_save();
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

/* Delete an exiting resource reservation */
extern int delete_resv(reservation_name_msg_t *resv_desc_ptr)
{
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;
	int rc = SLURM_SUCCESS;

#ifdef _RESV_DEBUG
	info("delete_resv: Name=%s", resv_desc_ptr->name);
#endif

	iter = list_iterator_create(resv_list);
	if (!iter)
		fatal("malloc: list_iterator_create");
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if (strcmp(resv_ptr->name, resv_desc_ptr->name))
			continue;
		if (_is_resv_used(resv_ptr)) {
			rc = ESLURM_RESERVATION_BUSY;
			break;
		}
		_deleted_resv(resv_ptr);
		list_delete_item(iter);
		last_resv_update = time(NULL);
		break;
	}
	list_iterator_destroy(iter);

	if (!resv_ptr) {
		info("Reservation %s not found for deletion",
		     resv_desc_ptr->name);
		return ESLURM_RESERVATION_INVALID;
	}

	schedule_resv_save();
	return rc;
}

/* Dump the reservation records to a buffer */
extern void show_resv(char **buffer_ptr, int *buffer_size, uid_t uid)
{
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;
	uint32_t resv_packed;
	int tmp_offset;
	Buf buffer;
	time_t now = time(NULL);
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

	/* write individual reservation records */
	iter = list_iterator_create(resv_list);
	if (!iter)
		fatal("malloc: list_iterator_create");
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		_pack_resv(resv_ptr, buffer, false);
		resv_packed++;
	}
	list_iterator_destroy(iter);

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
	time_t now = time(NULL);
	DEF_TIMERS;

	START_TIMER;
	if (!resv_list)
		resv_list = list_create(_del_resv_rec);

	/* write header: time */
	packstr(RESV_STATE_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack32(top_suffix, buffer);

	/* write reservation records to buffer */
	lock_slurmctld(resv_read_lock);
	iter = list_iterator_create(resv_list);
	if (!iter)
		fatal("malloc: list_iterator_create");
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if (resv_ptr->end_time > now) {
			_pack_resv(resv_ptr, buffer, true);
		} else {
			debug("Purging vestigial reservation record %s",
			      resv_ptr->name);
			list_delete_item(iter);
		}
	}
	list_iterator_destroy(iter);
	/* Maintain config read lock until we copy state_save_location *\
	\* unlock_slurmctld(resv_read_lock);          - see below      */

	/* write the buffer to file */
	old_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(old_file, "/resv_state.old");
	reg_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(reg_file, "/resv_state");
	new_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(new_file, "/resv_state.new");
	unlock_slurmctld(resv_read_lock);
	lock_state_files();
	log_fd = creat(new_file, 0600);
	if (log_fd == 0) {
		error("Can't save state, error creating file %s, %m",
		      new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
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
		fsync(log_fd);
		close(log_fd);
	}
	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		(void) link(reg_file, old_file);
		(void) unlink(reg_file);
		(void) link(new_file, reg_file);
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
	if ((resv_ptr->name == NULL) || (resv_ptr->name[0] == '\0')) {
		error("Read reservation without name");
		return false;
	}
	if (resv_ptr->type > RESERVE_TYPE_MAINT) {
		error("Reservation %s has invalid type (%u)",
		      resv_ptr->name, resv_ptr->type);
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
					 &account_cnt, &account_list);
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
	}
	if (resv_ptr->users) {
		int rc, user_cnt = 0;
		uid_t *user_list = NULL;
		rc = _build_uid_list(resv_ptr->users, 
				     &user_cnt, &user_list);
		if (rc) {
			error("Reservation %s has invalid users (%s)",
			      resv_ptr->name, resv_ptr->users);
			return false;
		}
		xfree(resv_ptr->user_list);
		resv_ptr->user_cnt  = user_cnt;
		resv_ptr->user_list = user_list;
	}
	if (resv_ptr->node_list) {		/* Change bitmap last */
		bitstr_t *node_bitmap;
		if (strcmp(resv_ptr->node_list, "ALL") == 0) {
			node_bitmap = bit_alloc(node_record_count);
			bit_nset(node_bitmap, 0, (node_record_count - 1));
		} else if (node_name2bitmap(resv_ptr->node_list, 
					    false, &node_bitmap)) {
			error("Reservation %s has invalid nodes (%s)",
			      resv_ptr->name, resv_ptr->node_list);
			return false;
		}
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
	time_t now = time(NULL);
	char *tmp;
	uint32_t res_num;

	iter = list_iterator_create(resv_list);
	if (!iter)
		fatal("malloc: list_iterator_create");
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if (resv_ptr->end_time < now) {
			debug("Purging vestigial reservation record %s",
			      resv_ptr->name);
			list_delete_item(iter);
		} else if (!_validate_one_reservation(resv_ptr)) {
			error("Purging invalid reservation record %s",
			      resv_ptr->name);
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
}

/*
 * Load the reservation state from file, recover on slurmctld restart. 
 *	execute this after loading the configuration file data.
 * IN recover - 0 = no change
 *              1 = validate existing (in memory) reservations
 *              2 = recover all reservation state from disk
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

	last_resv_update = time(NULL);
	if (recover == 0) {
		if (resv_list)
			_validate_all_reservations();
		else
			resv_list = list_create(_del_resv_rec);
		return SLURM_SUCCESS;
	}

	if (resv_list)
		list_flush(resv_list);
	else
		resv_list = list_create(_del_resv_rec);

	/* read the file */
	state_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(state_file, "/resv_state");
	lock_state_files();
	state_fd = open(state_file, O_RDONLY);
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
	if ((!ver_str) || (strcmp(ver_str, RESV_STATE_VERSION) != 0)) {
		error("************************************************************");
		error("Can not recover reservation state, data version incompatable");
		error("************************************************************");
		xfree(ver_str);
		free_buf(buffer);
		return EFAULT;
	}
	xfree(ver_str);
	safe_unpack_time(&now, buffer);
	safe_unpack32(&top_suffix, buffer);

	while (remaining_buf(buffer) > 0) {
		resv_ptr = xmalloc(sizeof(slurmctld_resv_t));
		xassert(resv_ptr->magic = RESV_MAGIC);	/* Sets value */
		safe_unpackstr_xmalloc(&resv_ptr->accounts,	
				       &uint32_tmp,	buffer);
		safe_unpack_time(&resv_ptr->end_time,	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->features,
				       &uint32_tmp, 	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->name,	&uint32_tmp, buffer);
		safe_unpack32(&resv_ptr->node_cnt,	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->node_list,
				       &uint32_tmp,	buffer);
		safe_unpackstr_xmalloc(&resv_ptr->partition,
				       &uint32_tmp, 	buffer);
		safe_unpack_time(&resv_ptr->start_time,	buffer);
		safe_unpack16(&resv_ptr->type,		buffer);
		safe_unpackstr_xmalloc(&resv_ptr->users,&uint32_tmp, buffer);

		/* Fields saved for internal use only (save state) */
		safe_unpackstr_xmalloc(&resv_ptr->assoc_list,	
				       &uint32_tmp,	buffer);
		safe_unpack32(&resv_ptr->cpu_cnt,	buffer);
		safe_unpack32(&resv_ptr->resv_id,	buffer);

		list_append(resv_list, resv_ptr);
		info("Recovered state of reservation %s", resv_ptr->name);
	}

	_validate_all_reservations();
	info("Recovered state of %d reservations", list_count(resv_list));
	free_buf(buffer);
	return error_code;

      unpack_error:
	_validate_all_reservations();
	error("Incomplete reservation data checkpoint file");
	info("Recovered state of %d reservations", list_count(resv_list));
	if (resv_ptr)
		_del_resv_rec(resv_ptr);
	free_buf(buffer);
	return EFAULT;
}

/*
 * Determine if a job request can use the specified reservations
 * IN/OUT job_ptr - job to validate, set its resv_id and resv_type
 * RET SLURM_SUCCESS or error code (not found or access denied)
*/
extern int validate_job_resv(struct job_record *job_ptr)
{
	slurmctld_resv_t *resv_ptr = NULL;
	int rc;

	xassert(job_ptr);

	if ((job_ptr->resv_name == NULL) || (job_ptr->resv_name[0] == '\0')) {
		xfree(job_ptr->resv_name);
		job_ptr->resv_id   = 0;
		job_ptr->resv_type = 0;
		return SLURM_SUCCESS;
	}

	if (!resv_list)
		return ESLURM_RESERVATION_INVALID;

	/* Find the named reservation */
	resv_ptr = (slurmctld_resv_t *) list_find_first (resv_list, 
			_find_resv_name, job_ptr->resv_name);
	if (!resv_ptr) {
		info("Reservation name not found (%s)", job_ptr->resv_name);
		return ESLURM_RESERVATION_INVALID;
	}

	rc = _valid_job_access_resv(job_ptr, resv_ptr);
	if (rc == SLURM_SUCCESS) {
		job_ptr->resv_id   = resv_ptr->resv_id;
		job_ptr->resv_type = resv_ptr->type;
	}
	return rc;
}

/* Determine if a job has access to a reservation
 * RET SLURM_SUCCESS if true, ESLURM_RESERVATION_ACCESS otherwise */
static int _valid_job_access_resv(struct job_record *job_ptr,
				  slurmctld_resv_t *resv_ptr)
{
	int i;

	/* Determine if we have access */
	if (/*association_enforced*/ 0) {
		/* FIXME: add association checks
		if (job_ptr->assoc_id in reservation association list)
			return SLURM_SUCCESS;
		*/
	} else {
		for (i=0; i<resv_ptr->user_cnt; i++) {
			if (job_ptr->user_id == resv_ptr->user_list[i])
				return SLURM_SUCCESS;
		}
		for (i=0; (i<resv_ptr->account_cnt) && job_ptr->account; i++) {
			if (resv_ptr->account_list[i] &&
			    (strcmp(job_ptr->account, 
				    resv_ptr->account_list[i]) == 0)) {
				return SLURM_SUCCESS;
			}
		}
	}
	info("Security violation, uid=%u attempt to use reservation %s",
	     job_ptr->user_id, resv_ptr->name);
	return ESLURM_RESERVATION_ACCESS;
}

/*
 * Determine which nodes a job can use based upon reservations
 * IN job_ptr      - job to test
 * IN/OUT when     - when we want the job to start (IN)
 *                   when the reservation is available (OUT)
 * OUT node_bitmap - nodes which the job can use, caller must free unless error
 * RET	SLURM_SUCCESS if runable now
 *	ESLURM_RESERVATION_ACCESS access to reservation denied
 *	ESLURM_RESERVATION_INVALID reservation invalid
 *	ESLURM_INVALID_TIME_VALUE reservation invalid at time "when"
 */
extern int job_test_resv(struct job_record *job_ptr, time_t *when,
			 bitstr_t **node_bitmap)
{
	slurmctld_resv_t * resv_ptr;
	time_t job_start_time, job_end_time;
	ListIterator iter;

	*node_bitmap = (bitstr_t *) NULL;

	if (job_ptr->resv_name) {
		resv_ptr = (slurmctld_resv_t *) list_find_first (resv_list, 
				_find_resv_name, job_ptr->resv_name);
		job_ptr->resv_ptr = resv_ptr;
		if (!resv_ptr)
			return ESLURM_RESERVATION_INVALID;
		if (_valid_job_access_resv(job_ptr, resv_ptr) != SLURM_SUCCESS)
			return ESLURM_RESERVATION_ACCESS;
		if (*when < resv_ptr->start_time) {
			/* reservation starts later */
			*when = resv_ptr->start_time;
			return ESLURM_INVALID_TIME_VALUE;
		}
		if (*when > resv_ptr->end_time) {
			/* reservation ended earlier */
			*when = resv_ptr->end_time;
			job_ptr->priority = 0;	/* administrative hold */
			return ESLURM_INVALID_TIME_VALUE;
		}
		*node_bitmap = bit_copy(resv_ptr->node_bitmap);
		return SLURM_SUCCESS;
	}

	job_ptr->resv_ptr = NULL;	/* should be redundant */
	*node_bitmap = bit_copy(avail_node_bitmap);
	if (list_count(resv_list) == 0)
		return SLURM_SUCCESS;

	job_start_time = job_end_time = *when;
	if (job_ptr->time_limit == INFINITE)
		job_end_time += 365 * 24 * 60 * 60;
	else if (job_ptr->time_limit != NO_VAL)
		job_end_time += (job_ptr->time_limit * 60);
	else {	/* partition time limit */
		if (job_ptr->part_ptr->max_time == INFINITE)
			job_end_time += 365 * 24 * 60 * 60;
		else
			job_end_time += (job_ptr->part_ptr->max_time * 60);
	}

	iter = list_iterator_create(resv_list);
	if (!iter)
		fatal("malloc: list_iterator_create");
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if ((resv_ptr->node_bitmap == NULL) ||
		    (resv_ptr->start_time > job_end_time) ||
		    (resv_ptr->end_time   < job_start_time))
			continue;
		bit_not(resv_ptr->node_bitmap);
		bit_and(*node_bitmap, resv_ptr->node_bitmap);
		bit_not(resv_ptr->node_bitmap);
	}
	list_iterator_destroy(iter);

	return SLURM_SUCCESS;
}
