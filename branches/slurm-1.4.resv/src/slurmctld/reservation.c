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

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

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

#include "src/slurmctld/slurmctld.h"

#define _RESV_DEBUG	1
#define RESV_MAGIC	0x3b82

time_t last_resv_update = (time_t) 0;

typedef struct slurmctld_resv {
	char *accounts;		/* names of accounts permitted to use	*/
	int account_cnt;	/* count of accounts permitted to use	*/
	char **account_list;	/* list of accounts permitted to use	*/
	time_t end_time;	/* end time of reservation		*/
	char *features;		/* required node features		*/
	uint16_t magic;		/* magic cookie, RESV_MAGIC		*/
	char *name;		/* name of reservation			*/
	uint32_t node_cnt;	/* count of nodes required		*/
	char *node_list;	/* list of reserved nodes or ALL	*/
	bitstr_t *node_bitmap;	/* bitmap of reserved nodes		*/
	char *partition;	/* name of partition to be used		*/
	struct part_record *part_ptr;	/* pointer to partition used	*/
	time_t start_time;	/* start time of reservation		*/
	uint16_t type;		/* see RESERVE_TYPE_* above		*/
	char *users;		/* names of users permitted to use	*/
	int user_cnt;		/* count of users permitted to use	*/
	uid_t *user_list;	/* array of users permitted to use	*/
} slurmctld_resv_t;

List resv_list = (List) NULL;

static void _del_resv_rec(void *x)
{
	int i;
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) x;

	if (resv_ptr) {
		xassert(resv_ptr->magic == RESV_MAGIC);
		xfree(resv_ptr->accounts);
		for (i=0; i<resv_ptr->account_cnt; i++)
			xfree(resv_ptr->account_list[i]);
		xfree(resv_ptr->account_list);
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

static int _find_resv_rec(void *x, void *key)
{
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) x;

	xassert(resv_ptr->magic == RESV_MAGIC);
	return strcmp(resv_ptr->name, (char *) key);
}

static void _dump_resv_req(reserve_request_msg_t *resv_ptr, char *mode)
{
#ifdef _RESV_DEBUG
	char start_str[32], end_str[32], *type_str;

	slurm_make_time_str(&resv_ptr->start_time,start_str,sizeof(start_str));
	slurm_make_time_str(&resv_ptr->end_time,  end_str,  sizeof(end_str));
	if (resv_ptr->type == RESERVE_TYPE_MAINT)
		type_str = "MAINT";
	else
		type_str = "";

	info("%s: Name=%s StartTime=%s EndTime=%s Type=%s NodeCnt=%u "
	     "NodeList=%s Features=%s PartitionName=%s Users=%u Accounts=%s",
	     resv_ptr->name, start_str, end_str, type_str, resv_ptr->node_cnt,
	     resv_ptr->node_list, resv_ptr->features, resv_ptr->partition, 
	     resv_ptr->users, resv_ptr->accounts);
#endif
}

static void _generate_resv_name(reserve_request_msg_t *resv_ptr)
{
	char *key, *name, *sep, tmp[14];
	ListIterator iter;
	int i, len, top_suffix = 0;
	slurmctld_resv_t * exist_resv_ptr;

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

	iter = list_iterator_create(resv_list);
	if (!iter)
		fatal("malloc: list_iterator_create");
	while ((exist_resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if (strncmp(name, exist_resv_ptr->name, len))
			continue;
		i = atoi(exist_resv_ptr->name + len);
		top_suffix = MAX(i, top_suffix);
	}
	list_iterator_destroy(iter);
	snprintf(tmp, sizeof(tmp), "%d", top_suffix);
	strcat(name, tmp);
}

/* Validate a comma delimited list of account names and build an array of
 * them
 * IN account       - a list of account names
 * OUT account_cnt  - number of accounts in the list
 * OUT account_list - list of the account names, 
 *		      CALLER MUST XFREE this plus each individual record
 * RETURN 0 on success */
static int _build_account_list(char *accounts, int *account_cnt, 
			       char ***account_list)
{
	char *last, *tmp, *tok;
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
#if 0
		/* Validate the account */
		if (failure)
			goto inval;
#endif
		ac_list[ac_cnt++] = xstrdup(tok);
		tok = strtok_r(NULL, ",", &last);
	}
	*account_cnt  = ac_cnt;
	*account_list = ac_list;
	return SLURM_SUCCESS;

#if 0
 inval:	for (i=0; i<ac_cnt; i++)
		xfree(ac_list[i]);
	xfree(ac_list);
	return ESLURM_INVALID_BANK_ACCOUNT;
#endif
}


/* Validate a comma delimited list of user names and build an array of
 * their UIDs
 * IN users      - a list of user names
 * OUT user_cnt  - number of UIDs in the list
 * OUT user_list - list of the user's uid, CALLER MUST XFREE;
 * RETURN 0 on success */
static int _build_uid_list(char *users, int *user_cnt, uid_t **user_list)
{
	char *last, *tmp, *tok;
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
			info("reservation request invalid user %s", tok);
			goto inval;
		}
		u_list[u_cnt++] = u_tmp;
		tok = strtok_r(NULL, ",", &last);
	}
	*user_cnt  = u_cnt;
	*user_list = u_list;
	return SLURM_SUCCESS;

 inval:	xfree(u_list);
	return ESLURM_INVALID_BANK_ACCOUNT;
}

/* 
 * _pack_resv - dump all configuration information about a specific reservation
 *	in machine independent form (for network transmission)
 * IN resv_ptr - pointer to reservation for which information is requested
 * IN/OUT buffer - buffer in which data is placed, pointers automatically 
 *	updated
 * NOTE: if you make any changes here be sure to make the corresponding 
 *	to _unpack_resv_info_members() in common/slurm_protocol_pack.c
 */
void _pack_resv(struct slurmctld_resv *resv_ptr, Buf buffer)
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
	char **account_list;
	uid_t *user_list = NULL;

	_dump_resv_req(resv_desc_ptr, "create_resv");

	/* Validate the request */
	if (resv_desc_ptr->start_time != (time_t) NO_VAL) {
		if (resv_desc_ptr->start_time < (now - 60))
			rc = ESLURM_INVALID_TIME_VALUE;
			goto bad_parse;
	} else
		resv_desc_ptr->start_time = now;
	if (resv_desc_ptr->end_time != (time_t) NO_VAL) {
		if (resv_desc_ptr->end_time < (now - 60))
			rc = ESLURM_INVALID_TIME_VALUE;
			goto bad_parse;
	} else
		resv_desc_ptr->end_time = INFINITE;
	if (resv_desc_ptr->type == (uint16_t) NO_VAL)
		resv_desc_ptr->type = 0;
	else if (resv_desc_ptr->type > RESERVE_TYPE_MAINT) {
		info("Invalid reservation type %u ignored",
		      resv_desc_ptr->type);
		resv_desc_ptr->type = 0;
	}
	if (resv_desc_ptr->name) {
		resv_ptr = (slurmctld_resv_t *) list_find_first (resv_list, 
				_find_resv_rec, resv_desc_ptr->name);
		if (resv_ptr) {
			info("Duplicate reservation name %s create request",
			     resv_desc_ptr->name);
			rc = ESLURM_RESERVATION_INVALID;
			goto bad_parse;
		}
	} else
		_generate_resv_name(resv_desc_ptr);
	if (resv_desc_ptr->partition) {
		part_ptr = find_part_record(resv_desc_ptr->partition);
		if (!part_ptr) {
			rc = ESLURM_INVALID_PARTITION_NAME;
			goto bad_parse;
		}
	}
	if ((resv_desc_ptr->accounts == NULL) &&
	    (resv_desc_ptr->users == NULL)) {
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

	if (resv_desc_ptr->node_list) {
		if (strcmp(resv_desc_ptr->node_list, "ALL") == 0) {
			node_bitmap = bit_alloc(node_record_count);
			bit_nset(node_bitmap, 0, (node_record_count - 1));
		} else if (node_name2bitmap(resv_desc_ptr->node_list, 
					    false, &node_bitmap)) {
			rc = ESLURM_INVALID_NODE_NAME;
			goto bad_parse;
		}
	} else if (resv_desc_ptr->node_cnt == 0) {
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

	if (!resv_list)
		list_create(_del_resv_rec);
	list_append(resv_list, resv_ptr);
	last_resv_update = now;

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
	slurmctld_resv_t *resv_ptr;

	_dump_resv_req(resv_desc_ptr, "update_resv");

	/* Find the specified reservation */
	if ((resv_desc_ptr->name == NULL) || (!resv_list))
		return ESLURM_RESERVATION_INVALID;
	resv_ptr = (slurmctld_resv_t *) list_find_first (resv_list, 
			_find_resv_rec, resv_desc_ptr->name);
	if (!resv_ptr)
		return ESLURM_RESERVATION_INVALID;

	/* Process the request */
	last_resv_update = now;
	if (resv_desc_ptr->start_time != (time_t) NO_VAL) {
		if (resv_desc_ptr->start_time < (now - 60))
			return ESLURM_INVALID_TIME_VALUE;
		resv_ptr->start_time = resv_desc_ptr->start_time;
	}
	if (resv_desc_ptr->end_time != (time_t) NO_VAL) {
		if (resv_desc_ptr->end_time < (now - 60))
			return ESLURM_INVALID_TIME_VALUE;
		resv_ptr->end_time = resv_desc_ptr->end_time;
	}
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
		if (!part_ptr)
			return ESLURM_INVALID_PARTITION_NAME;
		resv_ptr->partition	= resv_desc_ptr->partition;
		resv_desc_ptr->partition = NULL; /* Nothing left to free */
		resv_ptr->part_ptr	= part_ptr;
	}
	if (resv_desc_ptr->node_cnt != NO_VAL)
		resv_ptr->node_cnt = resv_desc_ptr->node_cnt;
	if (resv_desc_ptr->accounts) {
		int account_cnt = 0, i, rc;
		char **account_list;
		rc = _build_account_list(resv_desc_ptr->accounts, 
					 &account_cnt, &account_list);
		if (rc)
			return rc;
		xfree(resv_ptr->accounts);
		for (i=0; i<resv_ptr->account_cnt; i++)
			xfree(resv_ptr->account_list[i]);
		xfree(resv_ptr->account_list);
		resv_ptr->accounts = resv_desc_ptr->accounts;
		resv_desc_ptr->accounts = NULL;	/* Nothing left to free */
		resv_ptr->account_cnt  = account_cnt;
		resv_ptr->account_list = account_list;
	}
	if (resv_desc_ptr->features) {
		xfree(resv_ptr->features);
		resv_ptr->features = resv_desc_ptr->features;
		resv_desc_ptr->features = NULL;	/* Nothing left to free */
	}
	if (resv_desc_ptr->users) {
		int rc, user_cnt = 0;
		uid_t *user_list = NULL;
		rc = _build_uid_list(resv_desc_ptr->users, 
				     &user_cnt, &user_list);
		if (rc)
			return rc;
		xfree(resv_ptr->users);
		xfree(resv_ptr->user_list);
		resv_ptr->users = resv_desc_ptr->users;
		resv_desc_ptr->users = NULL;	/* Nothing left to free */
		resv_ptr->user_cnt  = user_cnt;
		resv_ptr->user_list = user_list;
	}
	if (resv_desc_ptr->node_list) {		/* Change bitmap last */
		bitstr_t *node_bitmap;
		if (strcmp(resv_desc_ptr->node_list, "ALL") == 0) {
			node_bitmap = bit_alloc(node_record_count);
			bit_nset(node_bitmap, 0, (node_record_count - 1));
		} else if (node_name2bitmap(resv_desc_ptr->node_list, 
					    false, &node_bitmap)) {
			return ESLURM_INVALID_NODE_NAME;
		}
		xfree(resv_ptr->node_list);
		resv_ptr->node_list = resv_desc_ptr->node_list;
		resv_desc_ptr->node_list = NULL;  /* Nothing left to free */
		FREE_NULL_BITMAP(resv_ptr->node_bitmap);
		resv_ptr->node_bitmap = node_bitmap;
	}

	return SLURM_SUCCESS;
}

/* Delete an exiting resource reservation */
extern int delete_resv(reservation_name_msg_t *resv_desc_ptr)
{
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;

#ifdef _RESV_DEBUG
	info("delete_resv: Name=%s", resv_desc_ptr->name);
#endif

	iter = list_iterator_create(resv_list);
	if (!iter)
		fatal("malloc: list_iterator_create");
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		if (strcmp(resv_ptr->name, resv_desc_ptr->name))
			continue;
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
	return SLURM_SUCCESS;
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
		_pack_resv(resv_ptr, buffer);
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
}
