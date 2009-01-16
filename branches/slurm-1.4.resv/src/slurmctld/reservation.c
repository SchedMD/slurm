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
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/parse_time.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/slurmctld.h"

#define _RESV_DEBUG	1
#define RESV_MAGIC	0x3b82
typedef struct slurmctld_resv {
	char *accounts;		/* names of accounts permitted to use	*/
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
} slurmctld_resv_t;

List resv_list = (List) NULL;

static void _del_resv_rec(void *x)
{
	slurmctld_resv_t *resv_ptr = (slurmctld_resv_t *) x;

	if (resv_ptr) {
		xassert(resv_ptr->magic == RESV_MAGIC);
		xfree(resv_ptr->accounts);
		xfree(resv_ptr->features);
		xfree(resv_ptr->name);
		if (resv_ptr->node_bitmap)
			bit_free(resv_ptr->node_bitmap);
		xfree(resv_ptr->node_list);
		xfree(resv_ptr->partition);
		xfree(resv_ptr->users);
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

	/* Generate name prefix, presently based upon the first account
	 * name and some account is required */
	key = resv_ptr->accounts;
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

/* Create a resource reservation */
extern int create_resv(reserve_request_msg_t *resv_desc_ptr)
{
	time_t now = time(NULL);
	struct part_record *part_ptr = NULL;
	bitstr_t *node_bitmap;
	slurmctld_resv_t *resv_ptr;

	_dump_resv_req(resv_desc_ptr, "create_resv");

	/* Validate the request */
	if (resv_desc_ptr->accounts == NULL)
		return ESLURM_INVALID_BANK_ACCOUNT;
	if (resv_desc_ptr->start_time != (time_t) NO_VAL) {
		if (resv_desc_ptr->start_time < (now - 60))
			return ESLURM_INVALID_TIME_VALUE;
	} else
		resv_desc_ptr->start_time = now;
	if (resv_desc_ptr->end_time != (time_t) NO_VAL) {
		if (resv_desc_ptr->end_time < (now - 60))
			return ESLURM_INVALID_TIME_VALUE;
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
			return ESLURM_RESERVATION_INVALID;
		}
	} else
		_generate_resv_name(resv_desc_ptr);
	if (resv_desc_ptr->partition) {
		part_ptr = find_part_record(resv_desc_ptr->partition);
		if (!part_ptr)
			return ESLURM_INVALID_PARTITION_NAME;
	}
/* FIXME: Need to add validation for: accounts, users */


	/*
	 * IMPORTANT: keep node_bitmap generation last or 
	 * we must free it on failure
	 */
	if (resv_desc_ptr->node_list) {
		if (strcmp(resv_desc_ptr->node_list, "ALL") == 0) {
			node_bitmap = bit_alloc(node_record_count);
			bit_nset(node_bitmap, 0, (node_record_count - 1));
		} else if (node_name2bitmap(resv_desc_ptr->node_list, 
					    false, &node_bitmap))
			return ESLURM_INVALID_NODE_NAME;
	} else if (resv_desc_ptr->node_cnt == 0)
		return ESLURM_INVALID_NODE_NAME;
	/* IMPORTANT: See note above */

	/* Create a new reservation record */
	resv_ptr = xmalloc(sizeof(slurmctld_resv_t));
	resv_ptr->accounts	= resv_desc_ptr->accounts;
	resv_desc_ptr->accounts = NULL;		/* Nothing left to free */
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
	resv_desc_ptr->users 	= NULL;		/* Nothing left to free */

	if (!resv_list)
		list_create(_del_resv_rec);
	list_append(resv_list, resv_ptr);

	return SLURM_SUCCESS;
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

	/* Validate the request */
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
/* FIXME: Validate accounts value */
		xfree(resv_ptr->accounts);
		resv_ptr->accounts = resv_desc_ptr->accounts;
		resv_desc_ptr->accounts = NULL;	/* Nothing left to free */
	}
	if (resv_desc_ptr->features) {
		xfree(resv_ptr->features);
		resv_ptr->features = resv_desc_ptr->features;
		resv_desc_ptr->features = NULL;	/* Nothing left to free */
	}
	if (resv_desc_ptr->users) {
/* FIXME: Validate users value */
		xfree(resv_ptr->users);
		resv_ptr->users = resv_desc_ptr->users;
		resv_desc_ptr->users = NULL;	/* Nothing left to free */
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
		break;
	}
	list_iterator_destroy(iter);

	if (!resv_ptr)
		info("Reservation %s not found for deletion",
		     resv_desc_ptr->name);
		return ESLURM_RESERVATION_INVALID;
	}
	return SLURM_SUCCESS;
}
