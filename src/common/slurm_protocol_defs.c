/*****************************************************************************\
 *  slurm_protocol_defs.c - functions for initializing and releasing
 *	storage for RPC data structures. these are the functions used by
 *	the slurm daemons directly, not for user client use.
 *****************************************************************************
 *  Portions Copyright (C) 2010-2017 SchedMD LLC <https://www.schedmd.com>.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/cron.h"
#include "src/common/forward.h"
#include "src/common/job_options.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/power.h"
#include "src/common/select.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_ext_sensors.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/switch.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
** Define slurm-specific aliases for use by plugins, see slurm_xlator.h
** for details.
 */
strong_alias(preempt_mode_string, slurm_preempt_mode_string);
strong_alias(preempt_mode_num, slurm_preempt_mode_num);
strong_alias(job_reason_string, slurm_job_reason_string);
strong_alias(job_reason_num, slurm_job_reason_num);
strong_alias(job_share_string, slurm_job_share_string);
strong_alias(job_state_string, slurm_job_state_string);
strong_alias(job_state_string_compact, slurm_job_state_string_compact);
strong_alias(job_state_num, slurm_job_state_num);
strong_alias(valid_base_state, slurm_valid_base_state);
strong_alias(node_state_base_string, slurm_node_state_base_string);
strong_alias(node_state_flag_string, slurm_node_state_flag_string);
strong_alias(node_state_flag_string_single, slurm_node_state_flag_string_single);
strong_alias(node_state_string, slurm_node_state_string);
strong_alias(node_state_string_compact, slurm_node_state_string_compact);
strong_alias(node_state_string_complete, slurm_node_state_string_complete);
strong_alias(private_data_string, slurm_private_data_string);
strong_alias(accounting_enforce_string, slurm_accounting_enforce_string);
strong_alias(cray_nodelist2nids, slurm_cray_nodelist2nids);
strong_alias(reservation_flags_string, slurm_reservation_flags_string);
strong_alias(print_multi_line_string, slurm_print_multi_line_string);

typedef struct {
	uint32_t flag;
	const char *str;
} node_state_flags_t;

static const node_state_flags_t node_states[] = {
	{ NODE_STATE_DOWN, "DOWN" },
	{ NODE_STATE_IDLE, "IDLE" },
	{ NODE_STATE_ALLOCATED, "ALLOCATED" },
	{ NODE_STATE_ERROR, "ERROR" },
	{ NODE_STATE_MIXED, "MIXED" },
	{ NODE_STATE_FUTURE, "FUTURE" },
	{ NODE_STATE_UNKNOWN, "UNKNOWN" },
};

static const node_state_flags_t node_state_flags[] = {
	{ NODE_STATE_CLOUD, "CLOUD" },
	{ NODE_STATE_COMPLETING, "COMPLETING" },
	{ NODE_STATE_DRAIN, "DRAIN" },
	{ NODE_STATE_DYNAMIC_FUTURE, "DYNAMIC_FUTURE" },
	{ NODE_STATE_DYNAMIC_NORM, "DYNAMIC_NORM" },
	{ NODE_STATE_INVALID_REG, "INVALID_REG" },
	{ NODE_STATE_FAIL, "FAIL" },
	{ NODE_STATE_MAINT, "MAINTENANCE" },
	{ NODE_STATE_POWER_DOWN, "POWER_DOWN" },
	{ NODE_STATE_POWER_UP, "POWER_UP" },
	{ NODE_STATE_NET, "PERFCTRS" }, /* net performance counters */
	{ NODE_STATE_POWERED_DOWN, "POWERED_DOWN" },
	{ NODE_STATE_REBOOT_REQUESTED, "REBOOT_REQUESTED" },
	{ NODE_STATE_REBOOT_ISSUED, "REBOOT_ISSUED" },
	{ NODE_STATE_RES, "RESERVED" },
	{ NODE_RESUME, "RESUME" },
	{ NODE_STATE_NO_RESPOND, "NOT_RESPONDING" },
	{ NODE_STATE_PLANNED, "PLANNED" },
	{ NODE_STATE_POWERING_UP, "POWERING_UP" },
	{ NODE_STATE_POWERING_DOWN, "POWERING_DOWN" },
};


static void _free_all_front_end_info(front_end_info_msg_t *msg);

static void _free_all_job_info (job_info_msg_t *msg);

static void _free_all_node_info (node_info_msg_t *msg);

static void _free_all_partitions (partition_info_msg_t *msg);

static void  _free_all_reservations(reserve_info_msg_t *msg);

static void _free_all_step_info (job_step_info_response_msg_t *msg);

static char *_convert_to_id(char *name, bool gid)
{
	char *tmp_name;
	if (gid) {
		gid_t gid;
		if (gid_from_string( name, &gid )) {
			error("Invalid group id: %s", name);
			return NULL;
		}
		tmp_name = xstrdup_printf("%u", gid);
	} else {
		uid_t uid;
		if (uid_from_string( name, &uid )) {
			error("Invalid user id: %s", name);
			return NULL;
		}
		tmp_name = xstrdup_printf("%u", uid);
	}
	return tmp_name;
}

/*
 * slurm_msg_t_init - initialize a slurm message
 * OUT msg - pointer to the slurm_msg_t structure which will be initialized
 */
extern void slurm_msg_t_init(slurm_msg_t *msg)
{
	memset(msg, 0, sizeof(slurm_msg_t));

	msg->auth_uid = SLURM_AUTH_NOBODY;
	msg->conn_fd = -1;
	msg->msg_type = NO_VAL16;
	msg->protocol_version = NO_VAL16;

#ifndef NDEBUG
	msg->flags = drop_priv_flag;
#endif

	forward_init(&msg->forward);
}

/*
 * slurm_msg_t_copy - initialize a slurm_msg_t structure "dest" with
 *	values from the "src" slurm_msg_t structure.
 * IN src - Pointer to the initialized message from which "dest" will
 *	be initialized.
 * OUT dest - Pointer to the slurm_msg_t which will be initialized.
 * NOTE: the "dest" structure will contain pointers into the contents of "src".
 */
extern void slurm_msg_t_copy(slurm_msg_t *dest, slurm_msg_t *src)
{
	slurm_msg_t_init(dest);
	dest->protocol_version = src->protocol_version;
	dest->forward = src->forward;
	dest->ret_list = src->ret_list;
	dest->forward_struct = src->forward_struct;

#if 0
	/* explicitly blow away the address. probably redundant */
	if (dest->orig_addr.ss_family == AF_INET6) {
		struct sockaddr_in6 *sin =
			(struct sockaddr_in6 *) &dest->orig_addr;
		memset(&sin->sin6_addr, 0, 16);
	} else {
		struct sockaddr_in *sin =
			(struct sockaddr_in *) &dest->orig_addr;
		sin->sin_addr.s_addr = 0;
	}
#endif

	dest->orig_addr.ss_family = AF_UNSPEC;
	if (src->auth_uid_set)
		slurm_msg_set_r_uid(dest, src->auth_uid);
}

/* here to add \\ to all \" in a string this needs to be xfreed later */
extern char *slurm_add_slash_to_quotes(char *str)
{
	char *dup, *copy = NULL;
	int len = 0;
	if (!str || !(len = strlen(str)))
		return NULL;

	/* make a buffer 2 times the size just to be safe */
	copy = dup = xmalloc((2 * len) + 1);
	if (copy)
		do if (*str == '\\' || *str == '\'' || *str == '"')
			   *dup++ = '\\';
		while ((*dup++ = *str++));

	return copy;
}

extern List slurm_copy_char_list(List char_list)
{
	List ret_list = NULL;
	char *tmp_char = NULL;
	ListIterator itr = NULL;

	if (!char_list || !list_count(char_list))
		return NULL;

	itr = list_iterator_create(char_list);
	ret_list = list_create(xfree_ptr);

	while ((tmp_char = list_next(itr)))
		list_append(ret_list, xstrdup(tmp_char));

	list_iterator_destroy(itr);

	return ret_list;
}

/*
 * ListFindF to find exact string in char pointer List.
 *
 * IN: x, list data (char *).
 * IN: key, string to be found.
 *
 * RET: 1 if found, 0 otherwise
 */
extern int slurm_find_char_exact_in_list(void *x, void *key)
{
	char *str1 = x;
	char *str2 = key;

	if (!xstrcmp(str1, str2))
		return 1;

	return 0;
}

extern int slurm_find_char_in_list(void *x, void *key)
{
	char *char1 = (char *)x;
	char *char2 = (char *)key;

	if (!xstrcasecmp(char1, char2))
		return 1;

	return 0;
}

static int _char_list_append_str(void *x, void *arg)
{
	char  *char_item = (char *)x;
	char **out_str   = (char **)arg;

	xassert(char_item);
	xassert(out_str);

	xstrfmtcat(*out_str, "%s%s", *out_str ? "," : "", char_item);

	return SLURM_SUCCESS;
}

extern char *slurm_char_list_to_xstr(List char_list)
{
	char *out = NULL;

	if (!char_list)
		return NULL;

	list_sort(char_list, (ListCmpF)slurm_sort_char_list_asc);
	list_for_each(char_list, _char_list_append_str, &out);

	return out;
}

static int _char_list_copy(void *item, void *dst)
{
	list_append((List)dst, xstrdup((char *)item));
	return SLURM_SUCCESS;
}

extern int slurm_char_list_copy(List dst, List src)
{
	xassert(dst);
	xassert(src);

	list_for_each(src, _char_list_copy, dst);

	return SLURM_SUCCESS;
}

extern int slurm_parse_char_list(List char_list, char *names, void *args,
				 int (*func_ptr)(List char_list, char *name,
						 void *args))
{
	int i = 0, start = 0, count = 0, result = 0;
	char quote_c = '\0';
	int quote = 0;
	char *tmp_names;

	if (!names)
		return 0;

	tmp_names = xstrdup(names);

	if ((tmp_names[i] == '\"') || (tmp_names[i] == '\'')) {
		quote_c = tmp_names[i];
		quote = 1;
		i++;
	}
	start = i;
	while (tmp_names[i]) {
		if (quote && (tmp_names[i] == quote_c)){
			tmp_names[i] = '\0';
			break;
		} else if ((tmp_names[i] == '\"') || (tmp_names[i] == '\''))
			tmp_names[i] = '`';
		else if (tmp_names[i] == ',') {
			if (i != start) {
				tmp_names[i] = '\0';
				result = (*func_ptr)(char_list,
						     tmp_names + start, args);
				tmp_names[i] = ',';

				if (result == SLURM_ERROR) {
					xfree(tmp_names);
					return SLURM_ERROR;
				} else
					count += result;
			}
			start = i + 1;
		}
		i++;
	}

	if (tmp_names[start]) {
		result = (*func_ptr)(char_list, tmp_names + start, args);
		if (result == SLURM_ERROR) {
			xfree(tmp_names);
			return SLURM_ERROR;
		} else
			count += result;
	}
	xfree(tmp_names);

	return count;
}

extern int slurm_addto_char_list(List char_list, char *names)
{
	return slurm_addto_char_list_with_case(char_list, names, true);
}

/* returns number of objects added to list */
extern int slurm_addto_char_list_with_case(List char_list, char *names,
					   bool lower_case_normalization)
{
	int i = 0, start = 0, cnt = 0;
	char *name = NULL;
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	int count = 0;
	bool brack_not = false;
	bool first_brack = false;
	char *this_node_name;
	char *tmp_this_node_name;
	hostlist_t host_list;

	if (!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	itr = list_iterator_create(char_list);
	if (names) {
		if (names[i] == '\"' || names[i] == '\'') {
			quote_c = names[i];
			quote = 1;
			i++;
		}
		start = i;
		cnt = list_count(char_list);
		while (names[i]) {
			//info("got %d - %d = %d", i, start, i-start);
			if (quote && (names[i] == quote_c))
				break;
			else if ((names[i] == '\"') || (names[i] == '\''))
				names[i] = '`';
			else if (names[i] == '[')
			       /*
				* Make sure there is a open bracket. This
				* check is to allow comma-separated notation
				* within the bracket (e.g. "linux[0-1,2]").
				*/
				first_brack = true;
			else if (names[i] == ',' && !first_brack) {
				/* Check that the string before , was
				 * not a [] notation value */
				if (!brack_not) {
					/*
					 * If there is a comma at the end just
					 * ignore it
					 */
					if (!names[i+1])
						break;

					/*
					 * Only add the non-blank names to the
					 * list
					 */
					if (i != start) {
						name = xstrndup(names+start,
								(i-start));
						/*
						* If we get a duplicate remove
						* the first one and tack this on
						* the end. This is needed for
						* get associations with QOS.
						*/
						if (list_find(
							itr,
							slurm_find_char_in_list,
							name)) {
							list_delete_item(itr);
						} else
							count++;
						if (lower_case_normalization)
							xstrtolower(name);
						list_append(char_list, name);

						list_iterator_reset(itr);
					}

					/*
					 * This line used to be "start = ++i".
					 * If we increment i too early, we will
					 * get issues with a list such as
					 * ",,this".
					 */
					start = i + 1;
				} else {
					brack_not = false;
					/*
					 * Skip over the "," so it is
					 * not included in the char list
					 */
					start = i + 1;
				}
			} else if (names[i] == ']') {
				brack_not = true;
				first_brack = false;
				name = xstrndup(names+start, ((i + 1)-start));
				//info("got %s %d", name, i-start);

				if ((host_list = hostlist_create(name))) {
					while ((tmp_this_node_name =
						hostlist_shift(host_list))) {
						/*
						 * Move from malloc-ed to
						 * xmalloc-ed memory
						 */
						this_node_name =
						    xstrdup(tmp_this_node_name);
						free(tmp_this_node_name);
						/*
						 * If we get a duplicate
						 * remove the first one and tack
						 * this on the end. This is
						 * needed for get associations
						 * with QOS.
						 */
						if (list_find(
							itr,
							slurm_find_char_in_list,
							this_node_name)) {
							list_delete_item(itr);
						} else
							count++;
						if (lower_case_normalization)
							xstrtolower(this_node_name);
						list_append(char_list,
							    this_node_name);

						list_iterator_reset(itr);

						start = i + 1;
					}
				}
				hostlist_destroy(host_list);
				xfree(name);
			}
			i++;
		}

		/* check for empty strings user='' etc */
		if ((cnt == list_count(char_list)) || (i - start)) {
			name = xstrndup(names+start, (i-start));
			/*
			 * If we get a duplicate remove the first one and
			 * tack this on the end. This is needed for get
			 * associations with QOS.
			 */
			if (list_find(itr, slurm_find_char_in_list, name)) {
				list_delete_item(itr);
			} else
				count++;
			if (lower_case_normalization)
				xstrtolower(name);
			list_append(char_list, name);
		}
	}

	list_iterator_destroy(itr);
	return count;
}

/* Parses string and converts names to either uid or gid list */
static int _slurm_addto_id_char_list_internal(List char_list, char *name,
					      void *x)
{
	bool gid = *(bool *)x;
	char *tmp_name = _convert_to_id(name, gid);
	if (!tmp_name) {
		list_flush(char_list);
		return SLURM_ERROR;
	}

	if (!list_find_first(char_list, slurm_find_char_in_list, tmp_name)) {
		list_append(char_list, tmp_name);
		return 1;
	} else {
		xfree(tmp_name);
		return 0;
	}
}

/* Parses string and converts names to either uid or gid list */
extern int slurm_addto_id_char_list(List char_list, char *names, bool gid)
{
	if (!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	return slurm_parse_char_list(char_list, names, &gid,
				     _slurm_addto_id_char_list_internal);
}

typedef struct {
	bool add_set;
	bool equal_set;
	int mode;
} char_list_internal_args_t;

static int _slurm_addto_mode_char_list_internal(List char_list, char *name,
						void *args_in)
{
	char *tmp_name = NULL;
	char_list_internal_args_t *args = args_in;
	char *err_msg = "You can't use '=' and '+' or '-' in the same line";

	int tmp_mode = args->mode;
	if ((name[0] == '+') || (name[0] == '-')) {
		tmp_mode = name[0];
		name++;
	}
	if (tmp_mode) {
		if (args->equal_set) {
			error("%s", err_msg);
			list_flush(char_list);
			return SLURM_ERROR;
		}
		args->add_set = 1;
		tmp_name = xstrdup_printf("%c%s", tmp_mode, name);
	} else {
		if (args->add_set) {
			error("%s", err_msg);
			list_flush(char_list);
			return SLURM_ERROR;
		}
		args->equal_set = 1;
		tmp_name = xstrdup_printf("%s", name);
	}

	if (!list_find_first(char_list, slurm_find_char_in_list, tmp_name)) {
		list_append(char_list, tmp_name);
		return 1;
	} else {
		xfree(tmp_name);
		return 0;
	}
}

/* Parses strings such as stra,+strb,-strc and appends the default mode to each
 * string in the list if no specific mode is listed.
 * RET: returns the number of items added to the list. -1 on error. */
extern int slurm_addto_mode_char_list(List char_list, char *names, int mode)
{
	char_list_internal_args_t args = {0};

	args.mode = mode;

	if (!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	return slurm_parse_char_list(char_list, names, &args,
			      	     _slurm_addto_mode_char_list_internal);
}

static int _addto_step_list_internal(List step_list, char *name, void *x)
{
	slurm_selected_step_t *selected_step = NULL;

	if (!isdigit(*name)) {
		fatal("Bad job/step specified: %s", name);
		return SLURM_ERROR;
	}

	selected_step = slurm_parse_step_str(name);

	if (!list_find_first(step_list, slurmdb_find_selected_step_in_list,
			     selected_step)) {
		list_append(step_list, selected_step);
		return 1;
	} else {
		slurm_destroy_selected_step(selected_step);
		return 0;
	}
}

/* returns number of objects added to list */
extern int slurm_addto_step_list(List step_list, char *names)
{
	if (!step_list) {
		error("No list was given to fill in");
		return 0;
	}

	return slurm_parse_char_list(step_list, names, NULL,
			      	     _addto_step_list_internal);
}

extern int slurm_sort_char_list_asc(void *v1, void *v2)
{
	char *name_a = *(char **)v1;
	char *name_b = *(char **)v2;
	int diff = xstrcmp(name_a, name_b);

	if (diff < 0)
		return -1;
	else if (diff > 0)
		return 1;

	return 0;
}

extern int slurm_sort_char_list_desc(void *v1, void *v2)
{
	char *name_a = *(char **)v1;
	char *name_b = *(char **)v2;
	int diff = xstrcmp(name_a, name_b);

	if (diff > 0)
		return -1;
	else if (diff < 0)
		return 1;

	return 0;
}

extern slurm_selected_step_t *slurm_parse_step_str(char *name)
{
	slurm_selected_step_t *selected_step;
	char *dot, *plus = NULL, *under;

	xassert(name);

	selected_step = xmalloc(sizeof(*selected_step));
	selected_step->step_id.step_het_comp = NO_VAL;

	if ((dot = xstrstr(name, "."))) {
		*dot++ = 0;
		/* can't use NO_VAL since that means all */
		if (!xstrcmp(dot, "batch"))
			selected_step->step_id.step_id = SLURM_BATCH_SCRIPT;
		else if (!xstrcmp(dot, "extern"))
			selected_step->step_id.step_id = SLURM_EXTERN_CONT;
		else if (!xstrcmp(dot, "interactive"))
			selected_step->step_id.step_id = SLURM_INTERACTIVE_STEP;
		else if (isdigit(*dot))
			selected_step->step_id.step_id = atoi(dot);
		else
			fatal("Bad step specified: %s", name);
		plus = xstrchr(dot, '+');
		if (plus) {
			/* het step */
			plus++;
			selected_step->step_id.step_het_comp =
				slurm_atoul(plus);
		}
	} else {
		debug2("No jobstep requested");
		selected_step->step_id.step_id = NO_VAL;
	}

	if ((under = xstrstr(name, "_"))) {
		*under++ = 0;
		if (isdigit(*under))
			selected_step->array_task_id = atoi(under);
		else
			fatal("Bad job array element specified: %s", name);
		selected_step->het_job_offset = NO_VAL;
	} else if (!plus && (plus = xstrstr(name, "+"))) {
		selected_step->array_task_id = NO_VAL;
		*plus++ = 0;
		if (isdigit(*plus))
			selected_step->het_job_offset = atoi(plus);
		else
			fatal("Bad hetjob offset specified: %s", name);
	} else {
		debug2("No jobarray or hetjob requested");
		selected_step->array_task_id = NO_VAL;
		selected_step->het_job_offset = NO_VAL;
	}

	selected_step->step_id.job_id = atoi(name);

	return selected_step;
}

extern resource_allocation_response_msg_t *
slurm_copy_resource_allocation_response_msg(
	resource_allocation_response_msg_t *msg)
{
	resource_allocation_response_msg_t *new;

	if (!msg)
		return NULL;

	new = xmalloc(sizeof(*msg));

	memcpy(new, msg, sizeof(*msg));
	new->account = xstrdup(msg->account);
	new->alias_list = xstrdup(msg->alias_list);
	new->batch_host = xstrdup(msg->batch_host);

	if (msg->cpus_per_node) {
		new->cpus_per_node = xcalloc(new->num_cpu_groups,
					     sizeof(*new->cpus_per_node));
		memcpy(new->cpus_per_node, msg->cpus_per_node,
		       new->num_cpu_groups * sizeof(*new->cpus_per_node));
	}

	if (msg->cpu_count_reps) {
		new->cpu_count_reps = xcalloc(new->num_cpu_groups,
					      sizeof(*new->cpu_count_reps));
		memcpy(new->cpu_count_reps, msg->cpu_count_reps,
		       new->num_cpu_groups * sizeof(*new->cpu_count_reps));
	}

	new->environment = env_array_copy((const char **)msg->environment);
	new->job_submit_user_msg = xstrdup(msg->job_submit_user_msg);
	if (msg->node_addr) {
		new->node_addr = xmalloc(sizeof(*new->node_addr));
		memcpy(new->node_addr, msg->node_addr, sizeof(*new->node_addr));
	}
	new->node_list = xstrdup(msg->node_list);
	new->partition = xstrdup(msg->partition);
	new->qos = xstrdup(msg->qos);
	new->resv_name = xstrdup(msg->resv_name);
	new->working_cluster_rec = NULL;
	return new;
}


extern void slurm_free_last_update_msg(last_update_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_init_reboot_msg(reboot_msg_t *msg, bool clear)
{
	xassert(msg);

	if (clear)
		memset(msg, 0, sizeof(reboot_msg_t));

	msg->next_state = NO_VAL;
}

extern void slurm_free_reboot_msg(reboot_msg_t * msg)
{
	if (msg) {
		xfree(msg->features);
		xfree(msg->node_list);
		xfree(msg->reason);
		xfree(msg);
	}
}

extern void slurm_free_shutdown_msg(shutdown_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_job_alloc_info_msg(job_alloc_info_msg_t * msg)
{
	if (msg) {
		xfree(msg->req_cluster);
		xfree(msg);
	}
}

extern void slurm_free_return_code_msg(return_code_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_reroute_msg(reroute_msg_t *msg)
{
	if (msg) {
		slurmdb_destroy_cluster_rec(msg->working_cluster_rec);
		xfree(msg);
	}
}

extern void slurm_free_batch_script_msg(char *msg)
{
	xfree(msg);
}

extern void slurm_free_job_id_msg(job_id_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_job_user_id_msg(job_user_id_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_step_id(slurm_step_id_t *msg)
{
	xfree(msg);
}

extern void slurm_free_job_id_request_msg(job_id_request_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_config_request_msg(config_request_msg_t *msg)
{
	if (msg) {
		xfree(msg);
	}
}

extern void slurm_free_config_response_msg(config_response_msg_t *msg)
{
	if (msg) {
		if (msg->config_files)
			list_destroy(msg->config_files);
		xfree(msg->config);
		xfree(msg->acct_gather_config);
		xfree(msg->cgroup_config);
		xfree(msg->cgroup_allowed_devices_file_config);
		xfree(msg->ext_sensors_config);
		xfree(msg->gres_config);
		xfree(msg->job_container_config);
		xfree(msg->knl_cray_config);
		xfree(msg->knl_generic_config);
		xfree(msg->plugstack_config);
		xfree(msg->topology_config);
		xfree(msg->xtra_config);
		xfree(msg->slurmd_spooldir);
		xfree(msg);
	}
}

extern void slurm_free_update_step_msg(step_update_request_msg_t * msg)
{
	if (msg) {
		xfree(msg);
	}
}

extern void slurm_destroy_selected_step(void *object)
{
	slurm_selected_step_t *step = (slurm_selected_step_t *)object;
	xfree(step);
}

extern void slurm_free_job_id_response_msg(job_id_response_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_job_step_kill_msg(job_step_kill_msg_t * msg)
{
	if (msg) {
		xfree(msg->sibling);
		xfree(msg->sjob_id);
		xfree(msg);
	}
}

extern void slurm_free_job_info_request_msg(job_info_request_msg_t *msg)
{
	if (msg) {
		FREE_NULL_LIST(msg->job_ids);
		xfree(msg);
	}
}

extern void slurm_free_job_step_info_request_msg(job_step_info_request_msg_t *msg)
{
	xfree(msg);
}

extern void slurm_free_front_end_info_request_msg
		(front_end_info_request_msg_t *msg)
{
	xfree(msg);
}

extern void slurm_free_node_info_request_msg(node_info_request_msg_t *msg)
{
	xfree(msg);
}

extern void slurm_free_node_info_single_msg(node_info_single_msg_t *msg)
{
	if (msg) {
		xfree(msg->node_name);
		xfree(msg);
	}
}

extern void slurm_free_part_info_request_msg(part_info_request_msg_t *msg)
{
	xfree(msg);
}

extern void slurm_free_job_desc_msg(job_desc_msg_t *msg)
{
	int i;

	if (msg) {
		xfree(msg->account);
		xfree(msg->acctg_freq);
		xfree(msg->admin_comment);
		xfree(msg->alloc_node);
		if (msg->argv) {
			for (i = 0; i < msg->argc; i++)
				xfree(msg->argv[i]);
		}
		xfree(msg->argv);
		FREE_NULL_BITMAP(msg->array_bitmap);
		xfree(msg->array_inx);
		xfree(msg->batch_features);
		xfree(msg->burst_buffer);
		xfree(msg->clusters);
		xfree(msg->comment);
		xfree(msg->container);
		xfree(msg->cpu_bind);
		xfree(msg->cpus_per_tres);
		free_cron_entry(msg->crontab_entry);
		xfree(msg->dependency);
		env_array_free(msg->environment);
		msg->environment = NULL;
		xfree(msg->extra);
		xfree(msg->exc_nodes);
		xfree(msg->features);
		xfree(msg->cluster_features);
		xfree(msg->job_id_str);
		xfree(msg->licenses);
		xfree(msg->mail_user);
		xfree(msg->mcs_label);
		xfree(msg->mem_bind);
		xfree(msg->mem_per_tres);
		xfree(msg->name);
		xfree(msg->network);
		xfree(msg->origin_cluster);
		xfree(msg->partition);
		xfree(msg->qos);
		xfree(msg->req_context);
		xfree(msg->req_nodes);
		xfree(msg->reservation);
		xfree(msg->resp_host);
		xfree(msg->script);
		free_buf(msg->script_buf);
		select_g_select_jobinfo_free(msg->select_jobinfo);
		msg->select_jobinfo = NULL;
		xfree(msg->selinux_context);
		xfree(msg->std_err);
		xfree(msg->std_in);
		xfree(msg->std_out);
		if (msg->spank_job_env) {
			for (i = 0; i < msg->spank_job_env_size; i++)
				xfree(msg->spank_job_env[i]);
			xfree(msg->spank_job_env);
		}
		xfree(msg->submit_line);
		xfree(msg->tres_bind);
		xfree(msg->tres_freq);
		xfree(msg->tres_req_cnt);
		xfree(msg->tres_per_job);
		xfree(msg->tres_per_node);
		xfree(msg->tres_per_socket);
		xfree(msg->tres_per_task);
		xfree(msg->wckey);
		xfree(msg->work_dir);
		xfree(msg->x11_magic_cookie);
		xfree(msg->x11_target);
		xfree(msg);
	}
}

extern void slurm_free_sib_msg(sib_msg_t *msg)
{
	if (msg) {
		free_buf(msg->data_buffer);
		xfree(msg->resp_host);
		if (msg->data)
			slurm_free_msg_data(msg->data_type, msg->data);
		xfree(msg);
	}
}

extern void slurm_free_dep_msg(dep_msg_t *msg)
{
	if (msg) {
		xfree(msg->dependency);
		xfree(msg->job_name);
	}
}

extern void slurm_free_dep_update_origin_msg(dep_update_origin_msg_t *msg)
{
	if (msg) {
		FREE_NULL_LIST(msg->depend_list);
	}
}

extern void slurm_free_prolog_launch_msg(prolog_launch_msg_t * msg)
{
	int i;

	if (msg) {
		xfree(msg->alias_list);
		FREE_NULL_LIST(msg->job_gres_info);
		xfree(msg->nodes);
		xfree(msg->partition);
		xfree(msg->std_err);
		xfree(msg->std_out);
		xfree(msg->work_dir);
		xfree(msg->user_name);

		xfree(msg->x11_alloc_host);
		xfree(msg->x11_magic_cookie);
		xfree(msg->x11_target);

		if (msg->spank_job_env) {
			for (i = 0; i < msg->spank_job_env_size; i++)
				xfree(msg->spank_job_env[i]);
			xfree(msg->spank_job_env);
		}
		slurm_cred_destroy(msg->cred);

		xfree(msg);
	}
}

extern void slurm_free_complete_prolog_msg(complete_prolog_msg_t * msg)
{
	xfree(msg->node_name);
	xfree(msg);
}

extern void slurm_free_job_launch_msg(batch_job_launch_msg_t * msg)
{
	int i;

	if (msg) {
		xfree(msg->account);
		xfree(msg->acctg_freq);
		xfree(msg->alias_list);
		if (msg->argv) {
			for (i = 0; i < msg->argc; i++)
				xfree(msg->argv[i]);
			xfree(msg->argv);
		}
		xfree(msg->container);
		xfree(msg->cpu_bind);
		xfree(msg->cpus_per_node);
		xfree(msg->cpu_count_reps);
		slurm_cred_destroy(msg->cred);
		if (msg->environment) {
			for (i = 0; i < msg->envc; i++)
				xfree(msg->environment[i]);
			xfree(msg->environment);
		}
		xfree(msg->gids);
		xfree(msg->nodes);
		xfree(msg->partition);
		xfree(msg->qos);
		xfree(msg->resv_name);
		xfree(msg->script);
		free_buf(msg->script_buf);
		select_g_select_jobinfo_free(msg->select_jobinfo);
		if (msg->spank_job_env) {
			for (i = 0; i < msg->spank_job_env_size; i++)
				xfree(msg->spank_job_env[i]);
			xfree(msg->spank_job_env);
		}
		xfree(msg->std_err);
		xfree(msg->std_in);
		xfree(msg->std_out);
		xfree(msg->tres_bind);
		xfree(msg->tres_freq);
		xfree(msg->user_name);
		xfree(msg->work_dir);
		xfree(msg);
	}
}

extern void slurm_free_job_info(job_info_t * job)
{
	if (job) {
		slurm_free_job_info_members(job);
		xfree(job);
	}
}

extern void slurm_free_job_info_members(job_info_t * job)
{
	int i;

	if (job) {
		xfree(job->account);
		xfree(job->alloc_node);
		FREE_NULL_BITMAP(job->array_bitmap);
		xfree(job->array_task_str);
		xfree(job->batch_features);
		xfree(job->batch_host);
		xfree(job->burst_buffer);
		xfree(job->burst_buffer_state);
		xfree(job->cluster);
		xfree(job->command);
		xfree(job->comment);
		xfree(job->container);
		xfree(job->cpus_per_tres);
		xfree(job->dependency);
		xfree(job->exc_nodes);
		xfree(job->exc_node_inx);
		xfree(job->features);
		xfree(job->fed_origin_str);
		xfree(job->fed_siblings_active_str);
		xfree(job->fed_siblings_viable_str);
		xfree(job->gres_total);
		if (job->gres_detail_str) {
			for (i = 0; i < job->gres_detail_cnt; i++)
				xfree(job->gres_detail_str[i]);
			xfree(job->gres_detail_str);
		}
		xfree(job->het_job_id_set);
		xfree(job->licenses);
		xfree(job->mail_user);
		xfree(job->mcs_label);
		xfree(job->mem_per_tres);
		xfree(job->name);
		xfree(job->network);
		xfree(job->node_inx);
		xfree(job->nodes);
		xfree(job->sched_nodes);
		xfree(job->partition);
		xfree(job->qos);
		xfree(job->req_node_inx);
		xfree(job->req_nodes);
		xfree(job->resv_name);
		select_g_select_jobinfo_free(job->select_jobinfo);
		job->select_jobinfo = NULL;
		free_job_resources(&job->job_resrcs);
		xfree(job->selinux_context);
		xfree(job->state_desc);
		xfree(job->std_err);
		xfree(job->std_in);
		xfree(job->std_out);
		xfree(job->tres_alloc_str);
		xfree(job->tres_bind);
		xfree(job->tres_freq);
		xfree(job->tres_per_job);
		xfree(job->tres_per_node);
		xfree(job->tres_per_socket);
		xfree(job->tres_per_task);
		xfree(job->tres_req_str);
		xfree(job->user_name);
		xfree(job->wckey);
		xfree(job->work_dir);
	}
}


extern void slurm_free_acct_gather_node_resp_msg(
	acct_gather_node_resp_msg_t *msg)
{
	if (msg) {
		xfree(msg->node_name);
		acct_gather_energy_destroy(msg->energy);
		xfree(msg);
	}
}

extern void slurm_free_acct_gather_energy_req_msg(
	acct_gather_energy_req_msg_t *msg)
{
	if (msg) {
		xfree(msg);
	}
}

extern void slurm_free_node_registration_status_msg(
	slurm_node_registration_status_msg_t * msg)
{
	if (msg) {
		xfree(msg->arch);
		xfree(msg->dynamic_conf);
		xfree(msg->dynamic_feature);
		xfree(msg->cpu_spec_list);
		if (msg->energy)
			acct_gather_energy_destroy(msg->energy);
		xfree(msg->features_active);
		xfree(msg->features_avail);
		xfree(msg->hostname);
		if (msg->gres_info)
			free_buf(msg->gres_info);
		xfree(msg->node_name);
		xfree(msg->os);
		xfree(msg->step_id);
		xfree(msg->version);
		xfree(msg);
	}
}

extern void slurm_free_node_reg_resp_msg(
	slurm_node_reg_resp_msg_t *msg)
{
	if (!msg)
		return;

	xfree(msg->node_name);
	FREE_NULL_LIST(msg->tres_list);
	xfree(msg);
}

extern void slurm_free_update_front_end_msg(update_front_end_msg_t * msg)
{
	if (msg) {
		xfree(msg->name);
		xfree(msg->reason);
		xfree(msg);
	}
}

extern void slurm_free_update_node_msg(update_node_msg_t * msg)
{
	if (msg) {
		xfree(msg->comment);
		xfree(msg->extra);
		xfree(msg->features);
		xfree(msg->features_act);
		xfree(msg->gres);
		xfree(msg->node_addr);
		xfree(msg->node_hostname);
		xfree(msg->node_names);
		xfree(msg->reason);
		xfree(msg);
	}
}

extern void slurm_free_update_part_msg(update_part_msg_t * msg)
{
	if (msg) {
		slurm_free_partition_info_members((partition_info_t *)msg);
		xfree(msg);
	}
}

extern void slurm_free_delete_part_msg(delete_part_msg_t * msg)
{
	if (msg) {
		xfree(msg->name);
		xfree(msg);
	}
}

extern void slurm_free_resv_desc_msg_part(resv_desc_msg_t *msg,
					  uint32_t res_free_flags)
{
	if (!msg)
		return;

	if (res_free_flags & RESV_FREE_STR_USER)
		xfree(msg->users);
	if (res_free_flags & RESV_FREE_STR_ACCT)
		xfree(msg->accounts);
	if (res_free_flags & RESV_FREE_STR_TRES_BB)
		xfree(msg->burst_buffer);
	if (res_free_flags & RESV_FREE_STR_TRES_CORE)
		xfree(msg->core_cnt);
	if (res_free_flags & RESV_FREE_STR_TRES_LIC)
		xfree(msg->licenses);
	if (res_free_flags & RESV_FREE_STR_TRES_NODE)
		xfree(msg->node_cnt);
	if (res_free_flags & RESV_FREE_STR_GROUP)
		xfree(msg->groups);
}

extern void slurm_free_resv_desc_msg(resv_desc_msg_t * msg)
{
	if (msg) {
		xfree(msg->features);
		xfree(msg->name);
		xfree(msg->node_list);
		xfree(msg->partition);

		slurm_free_resv_desc_msg_part(msg, 0xffffffff);

		xfree(msg);
	}
}

extern void slurm_free_resv_name_msg(reservation_name_msg_t * msg)
{
	if (msg) {
		xfree(msg->name);
		xfree(msg);
	}
}

extern void slurm_free_resv_info_request_msg(resv_info_request_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_job_step_create_request_msg(
		job_step_create_request_msg_t *msg)
{
	if (msg) {
		xfree(msg->container);
		xfree(msg->cpus_per_tres);
		xfree(msg->exc_nodes);
		xfree(msg->features);
		xfree(msg->host);
		xfree(msg->mem_per_tres);
		xfree(msg->name);
		xfree(msg->network);
		xfree(msg->node_list);
		xfree(msg->step_het_grps);
		xfree(msg->submit_line);
		xfree(msg->tres_bind);
		xfree(msg->tres_freq);
		xfree(msg->tres_per_step);
		xfree(msg->tres_per_node);
		xfree(msg->tres_per_socket);
		xfree(msg->tres_per_task);
		xfree(msg);
	}
}

extern void slurm_free_complete_job_allocation_msg(
	complete_job_allocation_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_complete_batch_script_msg(
		complete_batch_script_msg_t * msg)
{
	if (msg) {
		jobacctinfo_destroy(msg->jobacct);
		xfree(msg->node_name);
		xfree(msg);
	}
}


extern void slurm_free_launch_tasks_response_msg(
		launch_tasks_response_msg_t *msg)
{
	if (msg) {
		xfree(msg->node_name);
		xfree(msg->local_pids);
		xfree(msg->task_ids);
		xfree(msg);
	}
}

extern void slurm_free_kill_job_msg(kill_job_msg_t * msg)
{
	if (msg) {
		int i;
		slurm_cred_destroy(msg->cred);
		xfree(msg->details);
		FREE_NULL_LIST(msg->job_gres_info);
		xfree(msg->nodes);
		select_g_select_jobinfo_free(msg->select_jobinfo);
		msg->select_jobinfo = NULL;

		if (msg->spank_job_env) {
			for (i = 0; i < msg->spank_job_env_size; i++)
				xfree(msg->spank_job_env[i]);
			xfree(msg->spank_job_env);
		}
		xfree(msg->work_dir);
		xfree(msg);
	}
}

extern void slurm_free_task_exit_msg(task_exit_msg_t * msg)
{
	if (msg) {
		xfree(msg->task_id_list);
		xfree(msg);
	}
}

extern void slurm_free_launch_tasks_request_msg(launch_tasks_request_msg_t * msg)
{
	int i;

	if (msg == NULL)
		return;

	slurm_cred_destroy(msg->cred);

	if (msg->env) {
		for (i = 0; i < msg->envc; i++) {
			xfree(msg->env[i]);
		}
		xfree(msg->env);
	}
	xfree(msg->acctg_freq);
	xfree(msg->user_name);
	xfree(msg->alias_list);
	xfree(msg->container);
	xfree(msg->cwd);
	xfree(msg->cpu_bind);
	xfree(msg->mem_bind);
	if (msg->argv) {
		for (i = 0; i < msg->argc; i++) {
			xfree(msg->argv[i]);
		}
		xfree(msg->argv);
	}
	if (msg->spank_job_env) {
		for (i = 0; i < msg->spank_job_env_size; i++) {
			xfree(msg->spank_job_env[i]);
		}
		xfree(msg->spank_job_env);
	}
	if (msg->global_task_ids) {
		for (i = 0; i < msg->nnodes; i++) {
			xfree(msg->global_task_ids[i]);
		}
		xfree(msg->global_task_ids);
	}
	xfree(msg->gids);
	xfree(msg->het_job_node_list);
	xfree(msg->het_job_task_cnts);
	if (msg->het_job_nnodes != NO_VAL) {
		for (i = 0; i < msg->het_job_nnodes; i++)
			xfree(msg->het_job_tids[i]);
		xfree(msg->het_job_tids);
	}
	xfree(msg->het_job_tid_offsets);
	xfree(msg->tasks_to_launch);
	xfree(msg->resp_port);
	xfree(msg->io_port);
	xfree(msg->global_task_ids);
	xfree(msg->ifname);
	xfree(msg->ofname);
	xfree(msg->efname);

	xfree(msg->task_prolog);
	xfree(msg->task_epilog);
	xfree(msg->complete_nodelist);

	xfree(msg->partition);

	if (msg->switch_job)
		switch_g_free_jobinfo(msg->switch_job);

	FREE_NULL_LIST(msg->options);

	if (msg->select_jobinfo)
		select_g_select_jobinfo_free(msg->select_jobinfo);

	xfree(msg->tres_bind);
	xfree(msg->tres_per_task);
	xfree(msg->tres_freq);
	xfree(msg->x11_alloc_host);
	xfree(msg->x11_magic_cookie);
	xfree(msg->x11_target);

	xfree(msg);
}

extern void slurm_free_reattach_tasks_request_msg(
		reattach_tasks_request_msg_t *msg)
{
	if (msg) {
		xfree(msg->resp_port);
		xfree(msg->io_port);
		slurm_cred_destroy(msg->cred);
		xfree(msg);
	}
}

extern void slurm_free_reattach_tasks_response_msg(
		reattach_tasks_response_msg_t *msg)
{
	int i;

	if (msg) {
		xfree(msg->node_name);
		xfree(msg->local_pids);
		xfree(msg->gtids);
		if (msg->executable_names) {
			for (i = 0; i < msg->ntasks; i++) {
				xfree(msg->executable_names[i]);
			}
			xfree(msg->executable_names);
		}
		xfree(msg);
	}
}

extern void slurm_free_signal_tasks_msg(signal_tasks_msg_t *msg)
{
	xfree(msg);
}

extern void slurm_free_epilog_complete_msg(epilog_complete_msg_t * msg)
{
	if (msg) {
		xfree(msg->node_name);
		xfree(msg);
	}
}

extern void slurm_free_srun_job_complete_msg(
		srun_job_complete_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_srun_ping_msg(srun_ping_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_net_forward_msg(net_forward_msg_t *msg)
{
	if (msg) {
		xfree(msg->target);
		xfree(msg);
	}
}

extern void slurm_free_srun_node_fail_msg(srun_node_fail_msg_t * msg)
{
	if (msg) {
		xfree(msg->nodelist);
		xfree(msg);
	}
}

extern void slurm_free_srun_step_missing_msg(srun_step_missing_msg_t * msg)
{
	if (msg) {
		xfree(msg->nodelist);
		xfree(msg);
	}
}

extern void slurm_free_srun_timeout_msg(srun_timeout_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_srun_user_msg(srun_user_msg_t * user_msg)
{
	if (user_msg) {
		xfree(user_msg->msg);
		xfree(user_msg);
	}
}

extern void slurm_free_suspend_msg(suspend_msg_t *msg)
{
	if (msg) {
		xfree(msg->job_id_str);
		xfree(msg);
	}
}

extern void slurm_free_top_job_msg(top_job_msg_t *msg)
{
	if (msg) {
		xfree(msg->job_id_str);
		xfree(msg);
	}
}

extern void slurm_free_token_request_msg(token_request_msg_t *msg)
{
	if (msg) {
		xfree(msg->username);
		xfree(msg);
	}
}

extern void slurm_free_token_response_msg(token_response_msg_t *msg)
{
	if (msg) {
		xfree(msg->token);
		xfree(msg);
	}
}

extern void
slurm_free_requeue_msg(requeue_msg_t *msg)
{
	if (msg) {
		xfree(msg->job_id_str);
		xfree(msg);
	}
}

extern void slurm_free_suspend_int_msg(suspend_int_msg_t *msg)
{
	if (msg) {
		switch_g_job_suspend_info_free(msg->switch_info);
		xfree(msg);
	}
}

extern void slurm_free_stats_response_msg(stats_info_response_msg_t *msg)
{
	int i;
	if (msg) {
		xfree(msg->rpc_type_id);
		xfree(msg->rpc_type_cnt);
		xfree(msg->rpc_type_time);
		xfree(msg->rpc_user_id);
		xfree(msg->rpc_user_cnt);
		xfree(msg->rpc_user_time);
		xfree(msg->rpc_queue_type_id);
		xfree(msg->rpc_queue_count);
		xfree(msg->rpc_dump_types);
		for (i = 0; i < msg->rpc_dump_count; i++) {
			xfree(msg->rpc_dump_hostlist[i]);
		}
		xfree(msg->rpc_dump_hostlist);
		xfree(msg);
	}
}

/* Free job array oriented response with individual return codes by task ID */
extern void slurm_free_job_array_resp(job_array_resp_msg_t *msg)
{
	uint32_t i;

	if (msg) {
		if (msg->job_array_id) {
			for (i = 0; i < msg->job_array_count; i++)
				xfree(msg->job_array_id[i]);
			xfree(msg->job_array_id);
		}
		xfree(msg->error_code);
		xfree(msg);
	}
}

/* Given a job's reason for waiting, return a descriptive string */
extern char *job_reason_string(enum job_state_reason inx)
{
	static char val[32];

	switch (inx) {
	case WAIT_NO_REASON:
		return "None";
	case WAIT_PROLOG:
		return "Prolog";
	case WAIT_PRIORITY:
		return "Priority";
	case WAIT_DEPENDENCY:
		return "Dependency";
	case WAIT_RESOURCES:
		return "Resources";
	case WAIT_PART_NODE_LIMIT:
		return "PartitionNodeLimit";
	case WAIT_PART_TIME_LIMIT:
		return "PartitionTimeLimit";
	case WAIT_PART_DOWN:
		return "PartitionDown";
	case WAIT_PART_INACTIVE:
		return "PartitionInactive";
	case WAIT_HELD:
		return "JobHeldAdmin";
	case WAIT_HELD_USER:
		return "JobHeldUser";
	case WAIT_TIME:
		return "BeginTime";
	case WAIT_LICENSES:
		return "Licenses";
	case WAIT_ASSOC_JOB_LIMIT:
		return "AssociationJobLimit";
	case WAIT_ASSOC_RESOURCE_LIMIT:
		return "AssociationResourceLimit";
	case WAIT_ASSOC_TIME_LIMIT:
		return "AssociationTimeLimit";
	case WAIT_RESERVATION:
		return "Reservation";
	case WAIT_NODE_NOT_AVAIL:
		return "ReqNodeNotAvail";
	case WAIT_FRONT_END:
		return "FrontEndDown";
	case FAIL_DEFER:
		return "SchedDefer";
	case FAIL_DOWN_PARTITION:
		return "PartitionDown";
	case FAIL_DOWN_NODE:
		return "NodeDown";
	case FAIL_BAD_CONSTRAINTS:
		return "BadConstraints";
	case FAIL_SYSTEM:
		return "SystemFailure";
	case FAIL_LAUNCH:
		return "JobLaunchFailure";
	case FAIL_EXIT_CODE:
		return "NonZeroExitCode";
	case FAIL_TIMEOUT:
		return "TimeLimit";
	case FAIL_INACTIVE_LIMIT:
		return "InactiveLimit";
	case FAIL_ACCOUNT:
		return "InvalidAccount";
	case FAIL_QOS:
		return "InvalidQOS";
	case WAIT_QOS_THRES:
		return "QOSUsageThreshold";
	case WAIT_QOS_JOB_LIMIT:
		return "QOSJobLimit";
	case WAIT_QOS_RESOURCE_LIMIT:
		return "QOSResourceLimit";
	case WAIT_QOS_TIME_LIMIT:
		return "QOSTimeLimit";
	case WAIT_BLOCK_MAX_ERR:
		return "BlockMaxError";
	case WAIT_BLOCK_D_ACTION:
		return "BlockFreeAction";
	case WAIT_CLEANING:
		return "Cleaning";
	case WAIT_QOS:
		return "QOSNotAllowed";
	case WAIT_ACCOUNT:
		return "AccountNotAllowed";
	case WAIT_DEP_INVALID:
		return "DependencyNeverSatisfied";
	case WAIT_QOS_GRP_CPU:
		return "QOSGrpCpuLimit";
	case WAIT_QOS_GRP_CPU_MIN:
		return "QOSGrpCPUMinutesLimit";
	case WAIT_QOS_GRP_CPU_RUN_MIN:
		return "QOSGrpCPURunMinutesLimit";
	case WAIT_QOS_GRP_JOB:
		return"QOSGrpJobsLimit";
	case WAIT_QOS_GRP_MEM:
		return "QOSGrpMemLimit";
	case WAIT_QOS_GRP_NODE:
		return "QOSGrpNodeLimit";
	case WAIT_QOS_GRP_SUB_JOB:
		return "QOSGrpSubmitJobsLimit";
	case WAIT_QOS_GRP_WALL:
		return "QOSGrpWallLimit";
	case WAIT_QOS_MAX_CPU_PER_JOB:
		return "QOSMaxCpuPerJobLimit";
	case WAIT_QOS_MAX_CPU_MINS_PER_JOB:
		return "QOSMaxCpuMinutesPerJobLimit";
	case WAIT_QOS_MAX_NODE_PER_JOB:
		return "QOSMaxNodePerJobLimit";
	case WAIT_QOS_MAX_WALL_PER_JOB:
		return "QOSMaxWallDurationPerJobLimit";
	case WAIT_QOS_MAX_CPU_PER_USER:
		return "QOSMaxCpuPerUserLimit";
	case WAIT_QOS_MAX_JOB_PER_USER:
		return "QOSMaxJobsPerUserLimit";
	case WAIT_QOS_MAX_NODE_PER_USER:
		return "QOSMaxNodePerUserLimit";
	case WAIT_QOS_MAX_SUB_JOB:
		return "QOSMaxSubmitJobPerUserLimit";
	case WAIT_QOS_MIN_CPU:
		return "QOSMinCpuNotSatisfied";
	case WAIT_ASSOC_GRP_CPU:
		return "AssocGrpCpuLimit";
	case WAIT_ASSOC_GRP_CPU_MIN:
		return "AssocGrpCPUMinutesLimit";
	case WAIT_ASSOC_GRP_CPU_RUN_MIN:
		return "AssocGrpCPURunMinutesLimit";
	case WAIT_ASSOC_GRP_JOB:
		return"AssocGrpJobsLimit";
	case WAIT_ASSOC_GRP_MEM:
		return "AssocGrpMemLimit";
	case WAIT_ASSOC_GRP_NODE:
		return "AssocGrpNodeLimit";
	case WAIT_ASSOC_GRP_SUB_JOB:
		return "AssocGrpSubmitJobsLimit";
	case WAIT_ASSOC_GRP_WALL:
		return "AssocGrpWallLimit";
	case WAIT_ASSOC_MAX_JOBS:
		return "AssocMaxJobsLimit";
	case WAIT_ASSOC_MAX_CPU_PER_JOB:
		return "AssocMaxCpuPerJobLimit";
	case WAIT_ASSOC_MAX_CPU_MINS_PER_JOB:
		return "AssocMaxCpuMinutesPerJobLimit";
	case WAIT_ASSOC_MAX_NODE_PER_JOB:
		return "AssocMaxNodePerJobLimit";
	case WAIT_ASSOC_MAX_WALL_PER_JOB:
		return "AssocMaxWallDurationPerJobLimit";
	case WAIT_ASSOC_MAX_SUB_JOB:
		return "AssocMaxSubmitJobLimit";
	case WAIT_MAX_REQUEUE:
		return "JobHoldMaxRequeue";
	case WAIT_ARRAY_TASK_LIMIT:
		return "JobArrayTaskLimit";
	case WAIT_BURST_BUFFER_RESOURCE:
		return "BurstBufferResources";
	case WAIT_BURST_BUFFER_STAGING:
		return "BurstBufferStageIn";
	case FAIL_BURST_BUFFER_OP:
		return "BurstBufferOperation";
	case WAIT_POWER_NOT_AVAIL:
		return "PowerNotAvail";
	case WAIT_POWER_RESERVED:
		return "PowerReserved";
	case WAIT_ASSOC_GRP_UNK:
		return "AssocGrpUnknown";
	case WAIT_ASSOC_GRP_UNK_MIN:
		return "AssocGrpUnknownMinutes";
	case WAIT_ASSOC_GRP_UNK_RUN_MIN:
		return "AssocGrpUnknownRunMinutes";
	case WAIT_ASSOC_MAX_UNK_PER_JOB:
		return "AssocMaxUnknownPerJob";
	case WAIT_ASSOC_MAX_UNK_PER_NODE:
		return "AssocMaxUnknownPerNode";
	case WAIT_ASSOC_MAX_UNK_MINS_PER_JOB:
		return "AssocMaxUnknownMinutesPerJob";
	case WAIT_ASSOC_MAX_CPU_PER_NODE:
		return "AssocMaxCpuPerNode";
	case WAIT_ASSOC_GRP_MEM_MIN:
		return "AssocGrpMemMinutes";
	case WAIT_ASSOC_GRP_MEM_RUN_MIN:
		return "AssocGrpMemRunMinutes";
	case WAIT_ASSOC_MAX_MEM_PER_JOB:
		return "AssocMaxMemPerJob";
	case WAIT_ASSOC_MAX_MEM_PER_NODE:
		return "AssocMaxMemPerNode";
	case WAIT_ASSOC_MAX_MEM_MINS_PER_JOB:
		return "AssocMaxMemMinutesPerJob";
	case WAIT_ASSOC_GRP_NODE_MIN:
		return "AssocGrpNodeMinutes";
	case WAIT_ASSOC_GRP_NODE_RUN_MIN:
		return "AssocGrpNodeRunMinutes";
	case WAIT_ASSOC_MAX_NODE_MINS_PER_JOB:
		return "AssocMaxNodeMinutesPerJob";
	case WAIT_ASSOC_GRP_ENERGY:
		return "AssocGrpEnergy";
	case WAIT_ASSOC_GRP_ENERGY_MIN:
		return "AssocGrpEnergyMinutes";
	case WAIT_ASSOC_GRP_ENERGY_RUN_MIN:
		return "AssocGrpEnergyRunMinutes";
	case WAIT_ASSOC_MAX_ENERGY_PER_JOB:
		return "AssocMaxEnergyPerJob";
	case WAIT_ASSOC_MAX_ENERGY_PER_NODE:
		return "AssocMaxEnergyPerNode";
	case WAIT_ASSOC_MAX_ENERGY_MINS_PER_JOB:
		return "AssocMaxEnergyMinutesPerJob";
	case WAIT_ASSOC_GRP_GRES:
		return "AssocGrpGRES";
	case WAIT_ASSOC_GRP_GRES_MIN:
		return "AssocGrpGRESMinutes";
	case WAIT_ASSOC_GRP_GRES_RUN_MIN:
		return "AssocGrpGRESRunMinutes";
	case WAIT_ASSOC_MAX_GRES_PER_JOB:
		return "AssocMaxGRESPerJob";
	case WAIT_ASSOC_MAX_GRES_PER_NODE:
		return "AssocMaxGRESPerNode";
	case WAIT_ASSOC_MAX_GRES_MINS_PER_JOB:
		return "AssocMaxGRESMinutesPerJob";
	case WAIT_ASSOC_GRP_LIC:
		return "AssocGrpLicense";
	case WAIT_ASSOC_GRP_LIC_MIN:
		return "AssocGrpLicenseMinutes";
	case WAIT_ASSOC_GRP_LIC_RUN_MIN:
		return "AssocGrpLicenseRunMinutes";
	case WAIT_ASSOC_MAX_LIC_PER_JOB:
		return "AssocMaxLicensePerJob";
	case WAIT_ASSOC_MAX_LIC_MINS_PER_JOB:
		return "AssocMaxLicenseMinutesPerJob";
	case WAIT_ASSOC_GRP_BB:
		return "AssocGrpBB";
	case WAIT_ASSOC_GRP_BB_MIN:
		return "AssocGrpBBMinutes";
	case WAIT_ASSOC_GRP_BB_RUN_MIN:
		return "AssocGrpBBRunMinutes";
	case WAIT_ASSOC_MAX_BB_PER_JOB:
		return "AssocMaxBBPerJob";
	case WAIT_ASSOC_MAX_BB_PER_NODE:
		return "AssocMaxBBPerNode";
	case WAIT_ASSOC_MAX_BB_MINS_PER_JOB:
		return "AssocMaxBBMinutesPerJob";

	case WAIT_QOS_GRP_UNK:
		return "QOSGrpUnknown";
	case WAIT_QOS_GRP_UNK_MIN:
		return "QOSGrpUnknownMinutes";
	case WAIT_QOS_GRP_UNK_RUN_MIN:
		return "QOSGrpUnknownRunMinutes";
	case WAIT_QOS_MAX_UNK_PER_JOB:
		return "QOSMaxUnknownPerJob";
	case WAIT_QOS_MAX_UNK_PER_NODE:
		return "QOSMaxUnknownPerNode";
	case WAIT_QOS_MAX_UNK_PER_USER:
		return "QOSMaxUnknownPerUser";
	case WAIT_QOS_MAX_UNK_MINS_PER_JOB:
		return "QOSMaxUnknownMinutesPerJob";
	case WAIT_QOS_MIN_UNK:
		return "QOSMinUnknown";
	case WAIT_QOS_MAX_CPU_PER_NODE:
		return "QOSMaxCpuPerNode";
	case WAIT_QOS_GRP_MEM_MIN:
		return "QOSGrpMemoryMinutes";
	case WAIT_QOS_GRP_MEM_RUN_MIN:
		return "QOSGrpMemoryRunMinutes";
	case WAIT_QOS_MAX_MEM_PER_JOB:
		return "QOSMaxMemoryPerJob";
	case WAIT_QOS_MAX_MEM_PER_NODE:
		return "QOSMaxMemoryPerNode";
	case WAIT_QOS_MAX_MEM_PER_USER:
		return "QOSMaxMemoryPerUser";
	case WAIT_QOS_MAX_MEM_MINS_PER_JOB:
		return "QOSMaxMemoryMinutesPerJob";
	case WAIT_QOS_MIN_MEM:
		return "QOSMinMemory";
	case WAIT_QOS_GRP_NODE_MIN:
		return "QOSGrpNodeMinutes";
	case WAIT_QOS_GRP_NODE_RUN_MIN:
		return "QOSGrpNodeRunMinutes";
	case WAIT_QOS_MAX_NODE_MINS_PER_JOB:
		return "QOSMaxNodeMinutesPerJob";
	case WAIT_QOS_MIN_NODE:
		return "QOSMinNode";
	case WAIT_QOS_GRP_ENERGY:
		return "QOSGrpEnergy";
	case WAIT_QOS_GRP_ENERGY_MIN:
		return "QOSGrpEnergyMinutes";
	case WAIT_QOS_GRP_ENERGY_RUN_MIN:
		return "QOSGrpEnergyRunMinutes";
	case WAIT_QOS_MAX_ENERGY_PER_JOB:
		return "QOSMaxEnergyPerJob";
	case WAIT_QOS_MAX_ENERGY_PER_NODE:
		return "QOSMaxEnergyPerNode";
	case WAIT_QOS_MAX_ENERGY_PER_USER:
		return "QOSMaxEnergyPerUser";
	case WAIT_QOS_MAX_ENERGY_MINS_PER_JOB:
		return "QOSMaxEnergyMinutesPerJob";
	case WAIT_QOS_MIN_ENERGY:
		return "QOSMinEnergy";
	case WAIT_QOS_GRP_GRES:
		return "QOSGrpGRES";
	case WAIT_QOS_GRP_GRES_MIN:
		return "QOSGrpGRESMinutes";
	case WAIT_QOS_GRP_GRES_RUN_MIN:
		return "QOSGrpGRESRunMinutes";
	case WAIT_QOS_MAX_GRES_PER_JOB:
		return "QOSMaxGRESPerJob";
	case WAIT_QOS_MAX_GRES_PER_NODE:
		return "QOSMaxGRESPerNode";
	case WAIT_QOS_MAX_GRES_PER_USER:
		return "QOSMaxGRESPerUser";
	case WAIT_QOS_MAX_GRES_MINS_PER_JOB:
		return "QOSMaxGRESMinutesPerJob";
	case WAIT_QOS_MIN_GRES:
		return "QOSMinGRES";
	case WAIT_QOS_GRP_LIC:
		return "QOSGrpLicense";
	case WAIT_QOS_GRP_LIC_MIN:
		return "QOSGrpLicenseMinutes";
	case WAIT_QOS_GRP_LIC_RUN_MIN:
		return "QOSGrpLicenseRunMinutes";
	case WAIT_QOS_MAX_LIC_PER_JOB:
		return "QOSMaxLicensePerJob";
	case WAIT_QOS_MAX_LIC_PER_USER:
		return "QOSMaxLicensePerUser";
	case WAIT_QOS_MAX_LIC_MINS_PER_JOB:
		return "QOSMaxLicenseMinutesPerJob";
	case WAIT_QOS_MIN_LIC:
		return "QOSMinLicense";
	case WAIT_QOS_GRP_BB:
		return "QOSGrpBB";
	case WAIT_QOS_GRP_BB_MIN:
		return "QOSGrpBBMinutes";
	case WAIT_QOS_GRP_BB_RUN_MIN:
		return "QOSGrpBBRunMinutes";
	case WAIT_QOS_MAX_BB_PER_JOB:
		return "QOSMaxBBPerJob";
	case WAIT_QOS_MAX_BB_PER_NODE:
		return "QOSMaxBBPerNode";
	case WAIT_QOS_MAX_BB_PER_USER:
		return "QOSMaxBBPerUser";
	case WAIT_QOS_MAX_BB_MINS_PER_JOB:
		return "AssocMaxBBMinutesPerJob";
	case WAIT_QOS_MIN_BB:
		return "QOSMinBB";
	case FAIL_DEADLINE:
		return "DeadLine";
	case WAIT_QOS_MAX_BB_PER_ACCT:
		return "MaxBBPerAccount";
	case WAIT_QOS_MAX_CPU_PER_ACCT:
		return "MaxCpuPerAccount";
	case WAIT_QOS_MAX_ENERGY_PER_ACCT:
		return "MaxEnergyPerAccount";
	case WAIT_QOS_MAX_GRES_PER_ACCT:
		return "MaxGRESPerAccount";
	case WAIT_QOS_MAX_NODE_PER_ACCT:
		return "MaxNodePerAccount";
	case WAIT_QOS_MAX_LIC_PER_ACCT:
		return "MaxLicensePerAccount";
	case WAIT_QOS_MAX_MEM_PER_ACCT:
		return "MaxMemoryPerAccount";
	case WAIT_QOS_MAX_UNK_PER_ACCT:
		return "MaxUnknownPerAccount";
	case WAIT_QOS_MAX_JOB_PER_ACCT:
		return "MaxJobsPerAccount";
	case WAIT_QOS_MAX_SUB_JOB_PER_ACCT:
		return "MaxSubmitJobsPerAccount";
	case WAIT_PART_CONFIG:
		return "PartitionConfig";
	case WAIT_ACCOUNT_POLICY:
		return "AccountingPolicy";
	case WAIT_FED_JOB_LOCK:
		return "FedJobLock";
	case FAIL_OOM:
		return "OutOfMemory";
	case WAIT_PN_MEM_LIMIT:
		return "MaxMemPerLimit";
	case WAIT_ASSOC_GRP_BILLING:
		return "AssocGrpBilling";
	case WAIT_ASSOC_GRP_BILLING_MIN:
		return "AssocGrpBillingMinutes";
	case WAIT_ASSOC_GRP_BILLING_RUN_MIN:
		return "AssocGrpBillingRunMinutes";
	case WAIT_ASSOC_MAX_BILLING_PER_JOB:
		return "AssocMaxBillingPerJob";
	case WAIT_ASSOC_MAX_BILLING_PER_NODE:
		return "AssocMaxBillingPerNode";
	case WAIT_ASSOC_MAX_BILLING_MINS_PER_JOB:
		return "AssocMaxBillingMinutesPerJob";
	case WAIT_QOS_GRP_BILLING:
		return "QOSGrpBilling";
	case WAIT_QOS_GRP_BILLING_MIN:
		return "QOSGrpBillingMinutes";
	case WAIT_QOS_GRP_BILLING_RUN_MIN:
		return "QOSGrpBillingRunMinutes";
	case WAIT_QOS_MAX_BILLING_PER_JOB:
		return "QOSMaxBillingPerJob";
	case WAIT_QOS_MAX_BILLING_PER_NODE:
		return "QOSMaxBillingPerNode";
	case WAIT_QOS_MAX_BILLING_PER_USER:
		return "QOSMaxBillingPerUser";
	case WAIT_QOS_MAX_BILLING_MINS_PER_JOB:
		return "QOSMaxBillingMinutesPerJob";
	case WAIT_QOS_MAX_BILLING_PER_ACCT:
		return "MaxBillingPerAccount";
	case WAIT_QOS_MIN_BILLING:
		return "QOSMinBilling";
	case WAIT_RESV_DELETED:
		return "ReservationDeleted";
	default:
		snprintf(val, sizeof(val), "%d", inx);
		return val;
	}
}

/* Given a job's reason string for waiting, return enum job_state_reason */
extern enum job_state_reason job_reason_num(char *reason)
{
	if (!xstrcasecmp(reason, "None"))
		return WAIT_NO_REASON;
	if (!xstrcasecmp(reason, "Prolog"))
		return WAIT_PROLOG;
	if (!xstrcasecmp(reason, "Priority"))
		return WAIT_PRIORITY;
	if (!xstrcasecmp(reason, "Dependency"))
		return WAIT_DEPENDENCY;
	if (!xstrcasecmp(reason, "Resources"))
		return WAIT_RESOURCES;
	if (!xstrcasecmp(reason, "PartitionNodeLimit"))
		return WAIT_PART_NODE_LIMIT;
	if (!xstrcasecmp(reason, "PartitionTimeLimit"))
		return WAIT_PART_TIME_LIMIT;
	if (!xstrcasecmp(reason, "PartitionDown"))
		return WAIT_PART_DOWN;
	if (!xstrcasecmp(reason, "PartitionInactive"))
		return WAIT_PART_INACTIVE;
	if (!xstrcasecmp(reason, "JobHeldAdmin"))
		return WAIT_HELD;
	if (!xstrcasecmp(reason, "JobHeldUser"))
		return WAIT_HELD_USER;
	if (!xstrcasecmp(reason, "BeginTime"))
		return WAIT_TIME;
	if (!xstrcasecmp(reason, "Licenses"))
		return WAIT_LICENSES;
	if (!xstrcasecmp(reason, "AssociationJobLimit"))
		return WAIT_ASSOC_JOB_LIMIT;
	if (!xstrcasecmp(reason, "AssociationResourceLimit"))
		return WAIT_ASSOC_RESOURCE_LIMIT;
	if (!xstrcasecmp(reason, "AssociationTimeLimit"))
		return WAIT_ASSOC_TIME_LIMIT;
	if (!xstrcasecmp(reason, "Reservation"))
		return WAIT_RESERVATION;
	if (!xstrcasecmp(reason, "ReqNodeNotAvail"))
		return WAIT_NODE_NOT_AVAIL;
	if (!xstrcasecmp(reason, "FrontEndDown"))
		return WAIT_FRONT_END;
	if (!xstrcasecmp(reason, "PartitionDown"))
		return FAIL_DOWN_PARTITION;
	if (!xstrcasecmp(reason, "NodeDown"))
		return FAIL_DOWN_NODE;
	if (!xstrcasecmp(reason, "BadConstraints"))
		return FAIL_BAD_CONSTRAINTS;
	if (!xstrcasecmp(reason, "SystemFailure"))
		return FAIL_SYSTEM;
	if (!xstrcasecmp(reason, "JobLaunchFailure"))
		return FAIL_LAUNCH;
	if (!xstrcasecmp(reason, "NonZeroExitCode"))
		return FAIL_EXIT_CODE;
	if (!xstrcasecmp(reason, "TimeLimit"))
		return FAIL_TIMEOUT;
	if (!xstrcasecmp(reason, "InactiveLimit"))
		return FAIL_INACTIVE_LIMIT;
	if (!xstrcasecmp(reason, "InvalidAccount"))
		return FAIL_ACCOUNT;
	if (!xstrcasecmp(reason, "InvalidQOS"))
		return FAIL_QOS;
	if (!xstrcasecmp(reason, "QOSUsageThreshold"))
		return WAIT_QOS_THRES;
	if (!xstrcasecmp(reason, "QOSJobLimit"))
		return WAIT_QOS_JOB_LIMIT;
	if (!xstrcasecmp(reason, "QOSResourceLimit"))
		return WAIT_QOS_RESOURCE_LIMIT;
	if (!xstrcasecmp(reason, "QOSTimeLimit"))
		return WAIT_QOS_TIME_LIMIT;
	if (!xstrcasecmp(reason, "BlockMaxError"))
		return WAIT_BLOCK_MAX_ERR;
	if (!xstrcasecmp(reason, "BlockFreeAction"))
		return WAIT_BLOCK_D_ACTION;
	if (!xstrcasecmp(reason, "Cleaning"))
		return WAIT_CLEANING;
	if (!xstrcasecmp(reason, "QOSNotAllowed"))
		return WAIT_QOS;
	if (!xstrcasecmp(reason, "AccountNotAllowed"))
		return WAIT_ACCOUNT;
	if (!xstrcasecmp(reason, "DependencyNeverSatisfied"))
		return WAIT_DEP_INVALID;
	if (!xstrcasecmp(reason, "QOSGrpCpuLimit"))
		return WAIT_QOS_GRP_CPU;
	if (!xstrcasecmp(reason, "QOSGrpCPUMinutesLimit"))
		return WAIT_QOS_GRP_CPU_MIN;
	if (!xstrcasecmp(reason, "QOSGrpCPURunMinutesLimit"))
		return WAIT_QOS_GRP_CPU_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSGrpJobsLimit"))
		return WAIT_QOS_GRP_JOB;
	if (!xstrcasecmp(reason, "QOSGrpMemLimit"))
		return WAIT_QOS_GRP_MEM;
	if (!xstrcasecmp(reason, "QOSGrpNodeLimit"))
		return WAIT_QOS_GRP_NODE;
	if (!xstrcasecmp(reason, "QOSGrpSubmitJobsLimit"))
		return WAIT_QOS_GRP_SUB_JOB;
	if (!xstrcasecmp(reason, "QOSGrpWallLimit"))
		return WAIT_QOS_GRP_WALL;
	if (!xstrcasecmp(reason, "QOSMaxCpuPerJobLimit"))
		return WAIT_QOS_MAX_CPU_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxCpuMinutesPerJobLimit"))
		return WAIT_QOS_MAX_CPU_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxNodePerJobLimit"))
		return WAIT_QOS_MAX_NODE_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxWallDurationPerJobLimit"))
		return WAIT_QOS_MAX_WALL_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxCpuPerUserLimit"))
		return WAIT_QOS_MAX_CPU_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxJobsPerUserLimit"))
		return WAIT_QOS_MAX_JOB_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxNodePerUserLimit"))
		return WAIT_QOS_MAX_NODE_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxSubmitJobPerUserLimit"))
		return WAIT_QOS_MAX_SUB_JOB;
	if (!xstrcasecmp(reason, "QOSMinCpuNotSatisfied"))
		return WAIT_QOS_MIN_CPU;
	if (!xstrcasecmp(reason, "AssocGrpCpuLimit"))
		return WAIT_ASSOC_GRP_CPU;
	if (!xstrcasecmp(reason, "AssocGrpCPUMinutesLimit"))
		return WAIT_ASSOC_GRP_CPU_MIN;
	if (!xstrcasecmp(reason, "AssocGrpCPURunMinutesLimit"))
		return WAIT_ASSOC_GRP_CPU_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocGrpJobsLimit"))
		return WAIT_ASSOC_GRP_JOB;
	if (!xstrcasecmp(reason, "AssocGrpMemLimit"))
		return WAIT_ASSOC_GRP_MEM;
	if (!xstrcasecmp(reason, "AssocGrpNodeLimit"))
		return WAIT_ASSOC_GRP_NODE;
	if (!xstrcasecmp(reason, "AssocGrpSubmitJobsLimit"))
		return WAIT_ASSOC_GRP_SUB_JOB;
	if (!xstrcasecmp(reason, "AssocGrpWallLimit"))
		return WAIT_ASSOC_GRP_WALL;
	if (!xstrcasecmp(reason, "AssocMaxJobsLimit"))
		return WAIT_ASSOC_MAX_JOBS;
	if (!xstrcasecmp(reason, "AssocMaxCpuPerJobLimit"))
		return WAIT_ASSOC_MAX_CPU_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxCpuMinutesPerJobLimit"))
		return WAIT_ASSOC_MAX_CPU_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxNodePerJobLimit"))
		return WAIT_ASSOC_MAX_NODE_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxWallDurationPerJobLimit"))
		return WAIT_ASSOC_MAX_WALL_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxSubmitJobLimit"))
		return WAIT_ASSOC_MAX_SUB_JOB;
	if (!xstrcasecmp(reason, "JobHoldMaxRequeue"))
		return WAIT_MAX_REQUEUE;
	if (!xstrcasecmp(reason, "JobArrayTaskLimit"))
		return WAIT_ARRAY_TASK_LIMIT;
	if (!xstrcasecmp(reason, "BurstBufferResources"))
		return WAIT_BURST_BUFFER_RESOURCE;
	if (!xstrcasecmp(reason, "BurstBufferStageIn"))
		return WAIT_BURST_BUFFER_STAGING;
	if (!xstrcasecmp(reason, "BurstBufferOperation"))
		return FAIL_BURST_BUFFER_OP;
	if (!xstrcasecmp(reason, "PowerNotAvail"))
		return WAIT_POWER_NOT_AVAIL;
	if (!xstrcasecmp(reason, "PowerReserved"))
		return WAIT_POWER_RESERVED;
	if (!xstrcasecmp(reason, "AssocGrpUnknown"))
		return WAIT_ASSOC_GRP_UNK;
	if (!xstrcasecmp(reason, "AssocGrpUnknownMinutes"))
		return WAIT_ASSOC_GRP_UNK_MIN;
	if (!xstrcasecmp(reason, "AssocGrpUnknownRunMinutes"))
		return WAIT_ASSOC_GRP_UNK_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxUnknownPerJob"))
		return WAIT_ASSOC_MAX_UNK_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxUnknownPerNode"))
		return WAIT_ASSOC_MAX_UNK_PER_NODE;
	if (!xstrcasecmp(reason, "AssocMaxUnknownMinutesPerJob"))
		return WAIT_ASSOC_MAX_UNK_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxCpuPerNode"))
		return WAIT_ASSOC_MAX_CPU_PER_NODE;
	if (!xstrcasecmp(reason, "AssocGrpMemMinutes"))
		return WAIT_ASSOC_GRP_MEM_MIN;
	if (!xstrcasecmp(reason, "AssocGrpMemRunMinutes"))
		return WAIT_ASSOC_GRP_MEM_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxMemPerJob"))
		return WAIT_ASSOC_MAX_MEM_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxMemPerNode"))
		return WAIT_ASSOC_MAX_MEM_PER_NODE;
	if (!xstrcasecmp(reason, "AssocMaxMemMinutesPerJob"))
		return WAIT_ASSOC_MAX_MEM_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "AssocGrpNodeMinutes"))
		return WAIT_ASSOC_GRP_NODE_MIN;
	if (!xstrcasecmp(reason, "AssocGrpNodeRunMinutes"))
		return WAIT_ASSOC_GRP_NODE_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxNodeMinutesPerJob"))
		return WAIT_ASSOC_MAX_NODE_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "AssocGrpEnergy"))
		return WAIT_ASSOC_GRP_ENERGY;
	if (!xstrcasecmp(reason, "AssocGrpEnergyMinutes"))
		return WAIT_ASSOC_GRP_ENERGY_MIN;
	if (!xstrcasecmp(reason, "AssocGrpEnergyRunMinutes"))
		return WAIT_ASSOC_GRP_ENERGY_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxEnergyPerJob"))
		return WAIT_ASSOC_MAX_ENERGY_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxEnergyPerNode"))
		return WAIT_ASSOC_MAX_ENERGY_PER_NODE;
	if (!xstrcasecmp(reason, "AssocMaxEnergyMinutesPerJob"))
		return WAIT_ASSOC_MAX_ENERGY_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "AssocGrpGRES"))
		return WAIT_ASSOC_GRP_GRES;
	if (!xstrcasecmp(reason, "AssocGrpGRESMinutes"))
		return WAIT_ASSOC_GRP_GRES_MIN;
	if (!xstrcasecmp(reason, "AssocGrpGRESRunMinutes"))
		return WAIT_ASSOC_GRP_GRES_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxGRESPerJob"))
		return WAIT_ASSOC_MAX_GRES_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxGRESPerNode"))
		return WAIT_ASSOC_MAX_GRES_PER_NODE;
	if (!xstrcasecmp(reason, "AssocMaxGRESMinutesPerJob"))
		return WAIT_ASSOC_MAX_GRES_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "AssocGrpLicense"))
		return WAIT_ASSOC_GRP_LIC;
	if (!xstrcasecmp(reason, "AssocGrpLicenseMinutes"))
		return WAIT_ASSOC_GRP_LIC_MIN;
	if (!xstrcasecmp(reason, "AssocGrpLicenseRunMinutes"))
		return WAIT_ASSOC_GRP_LIC_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxLicensePerJob"))
		return WAIT_ASSOC_MAX_LIC_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxLicenseMinutesPerJob"))
		return WAIT_ASSOC_MAX_LIC_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "AssocGrpBB"))
		return WAIT_ASSOC_GRP_BB;
	if (!xstrcasecmp(reason, "AssocGrpBBMinutes"))
		return WAIT_ASSOC_GRP_BB_MIN;
	if (!xstrcasecmp(reason, "AssocGrpBBRunMinutes"))
		return WAIT_ASSOC_GRP_BB_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxBBPerJob"))
		return WAIT_ASSOC_MAX_BB_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxBBPerNode"))
		return WAIT_ASSOC_MAX_BB_PER_NODE;
	if (!xstrcasecmp(reason, "AssocMaxBBMinutesPerJob"))
		return WAIT_ASSOC_MAX_BB_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSGrpUnknown"))
		return WAIT_QOS_GRP_UNK;
	if (!xstrcasecmp(reason, "QOSGrpUnknownMinutes"))
		return WAIT_QOS_GRP_UNK_MIN;
	if (!xstrcasecmp(reason, "QOSGrpUnknownRunMinutes"))
		return WAIT_QOS_GRP_UNK_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxUnknownPerJob"))
		return WAIT_QOS_MAX_UNK_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxUnknownPerNode"))
		return WAIT_QOS_MAX_UNK_PER_NODE;
	if (!xstrcasecmp(reason, "QOSMaxUnknownPerUser"))
		return WAIT_QOS_MAX_UNK_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxUnknownMinutesPerJob"))
		return WAIT_QOS_MAX_UNK_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMinUnknown"))
		return WAIT_QOS_MIN_UNK;
	if (!xstrcasecmp(reason, "QOSMaxCpuPerNode"))
		return WAIT_QOS_MAX_CPU_PER_NODE;
	if (!xstrcasecmp(reason, "QOSGrpMemoryMinutes"))
		return WAIT_QOS_GRP_MEM_MIN;
	if (!xstrcasecmp(reason, "QOSGrpMemoryRunMinutes"))
		return WAIT_QOS_GRP_MEM_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxMemoryPerJob"))
		return WAIT_QOS_MAX_MEM_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxMemoryPerNode"))
		return WAIT_QOS_MAX_MEM_PER_NODE;
	if (!xstrcasecmp(reason, "QOSMaxMemoryPerUser"))
		return WAIT_QOS_MAX_MEM_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxMemoryMinutesPerJob"))
		return WAIT_QOS_MAX_MEM_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMinMemory"))
		return WAIT_QOS_MIN_MEM;
	if (!xstrcasecmp(reason, "QOSGrpNodeMinutes"))
		return WAIT_QOS_GRP_NODE_MIN;
	if (!xstrcasecmp(reason, "QOSGrpNodeRunMinutes"))
		return WAIT_QOS_GRP_NODE_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxNodeMinutesPerJob"))
		return WAIT_QOS_MAX_NODE_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMinNode"))
		return WAIT_QOS_MIN_NODE;
	if (!xstrcasecmp(reason, "QOSGrpEnergy"))
		return WAIT_QOS_GRP_ENERGY;
	if (!xstrcasecmp(reason, "QOSGrpEnergyMinutes"))
		return WAIT_QOS_GRP_ENERGY_MIN;
	if (!xstrcasecmp(reason, "QOSGrpEnergyRunMinutes"))
		return WAIT_QOS_GRP_ENERGY_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxEnergyPerJob"))
		return WAIT_QOS_MAX_ENERGY_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxEnergyPerNode"))
		return WAIT_QOS_MAX_ENERGY_PER_NODE;
	if (!xstrcasecmp(reason, "QOSMaxEnergyPerUser"))
		return WAIT_QOS_MAX_ENERGY_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxEnergyMinutesPerJob"))
		return WAIT_QOS_MAX_ENERGY_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMinEnergy"))
		return WAIT_QOS_MIN_ENERGY;
	if (!xstrcasecmp(reason, "QOSGrpGRES"))
		return WAIT_QOS_GRP_GRES;
	if (!xstrcasecmp(reason, "QOSGrpGRESMinutes"))
		return WAIT_QOS_GRP_GRES_MIN;
	if (!xstrcasecmp(reason, "QOSGrpGRESRunMinutes"))
		return WAIT_QOS_GRP_GRES_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxGRESPerJob"))
		return WAIT_QOS_MAX_GRES_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxGRESPerNode"))
		return WAIT_QOS_MAX_GRES_PER_NODE;
	if (!xstrcasecmp(reason, "QOSMaxGRESPerUser"))
		return WAIT_QOS_MAX_GRES_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxGRESMinutesPerJob"))
		return WAIT_QOS_MAX_GRES_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMinGRES"))
		return WAIT_QOS_MIN_GRES;
	if (!xstrcasecmp(reason, "QOSGrpLicense"))
		return WAIT_QOS_GRP_LIC;
	if (!xstrcasecmp(reason, "QOSGrpLicenseMinutes"))
		return WAIT_QOS_GRP_LIC_MIN;
	if (!xstrcasecmp(reason, "QOSGrpLicenseRunMinutes"))
		return WAIT_QOS_GRP_LIC_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxLicensePerJob"))
		return WAIT_QOS_MAX_LIC_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxLicensePerUser"))
		return WAIT_QOS_MAX_LIC_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxLicenseMinutesPerJob"))
		return WAIT_QOS_MAX_LIC_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMinLicense"))
		return WAIT_QOS_MIN_LIC;
	if (!xstrcasecmp(reason, "QOSGrpBB"))
		return WAIT_QOS_GRP_BB;
	if (!xstrcasecmp(reason, "QOSGrpBBMinutes"))
		return WAIT_QOS_GRP_BB_MIN;
	if (!xstrcasecmp(reason, "QOSGrpBBRunMinutes"))
		return WAIT_QOS_GRP_BB_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxBBPerJob"))
		return WAIT_QOS_MAX_BB_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxBBPerNode"))
		return WAIT_QOS_MAX_BB_PER_NODE;
	if (!xstrcasecmp(reason, "QOSMaxBBPerUser"))
		return WAIT_QOS_MAX_BB_PER_USER;
	if (!xstrcasecmp(reason, "AssocMaxBBMinutesPerJob"))
		return WAIT_QOS_MAX_BB_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMinBB"))
		return WAIT_QOS_MIN_BB;
	if (!xstrcasecmp(reason, "DeadLine"))
		return FAIL_DEADLINE;
	if (!xstrcasecmp(reason, "MaxBBPerAccount"))
		return WAIT_QOS_MAX_BB_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxCpuPerAccount"))
		return WAIT_QOS_MAX_CPU_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxEnergyPerAccount"))
		return WAIT_QOS_MAX_ENERGY_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxGRESPerAccount"))
		return WAIT_QOS_MAX_GRES_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxNodePerAccount"))
		return WAIT_QOS_MAX_NODE_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxLicensePerAccount"))
		return WAIT_QOS_MAX_LIC_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxMemoryPerAccount"))
		return WAIT_QOS_MAX_MEM_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxUnknownPerAccount"))
		return WAIT_QOS_MAX_UNK_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxJobsPerAccount"))
		return WAIT_QOS_MAX_JOB_PER_ACCT;
	if (!xstrcasecmp(reason, "MaxSubmitJobsPerAccount"))
		return WAIT_QOS_MAX_SUB_JOB_PER_ACCT;
	if (!xstrcasecmp(reason, "PartitionConfig"))
		return WAIT_PART_CONFIG;
	if (!xstrcasecmp(reason, "AccountingPolicy"))
		return WAIT_ACCOUNT_POLICY;
	if (!xstrcasecmp(reason, "FedJobLock"))
		return WAIT_FED_JOB_LOCK;
	if (!xstrcasecmp(reason, "OutOfMemory"))
		return FAIL_OOM;
	if (!xstrcasecmp(reason, "MaxMemPerLimit"))
		return WAIT_PN_MEM_LIMIT;
	if (!xstrcasecmp(reason, "AssocGrpBilling"))
		return WAIT_ASSOC_GRP_BILLING;
	if (!xstrcasecmp(reason, "AssocGrpBillingMinutes"))
		return WAIT_ASSOC_GRP_BILLING_MIN;
	if (!xstrcasecmp(reason, "AssocGrpBillingRunMinutes"))
		return WAIT_ASSOC_GRP_BILLING_RUN_MIN;
	if (!xstrcasecmp(reason, "AssocMaxBillingPerJob"))
		return WAIT_ASSOC_MAX_BILLING_PER_JOB;
	if (!xstrcasecmp(reason, "AssocMaxBillingPerNode"))
		return WAIT_ASSOC_MAX_BILLING_PER_NODE;
	if (!xstrcasecmp(reason, "AssocMaxBillingMinutesPerJob"))
		return WAIT_ASSOC_MAX_BILLING_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "QOSGrpBilling"))
		return WAIT_QOS_GRP_BILLING;
	if (!xstrcasecmp(reason, "QOSGrpBillingMinutes"))
		return WAIT_QOS_GRP_BILLING_MIN;
	if (!xstrcasecmp(reason, "QOSGrpBillingRunMinutes"))
		return WAIT_QOS_GRP_BILLING_RUN_MIN;
	if (!xstrcasecmp(reason, "QOSMaxBillingPerJob"))
		return WAIT_QOS_MAX_BILLING_PER_JOB;
	if (!xstrcasecmp(reason, "QOSMaxBillingPerNode"))
		return WAIT_QOS_MAX_BILLING_PER_NODE;
	if (!xstrcasecmp(reason, "QOSMaxBillingPerUser"))
		return WAIT_QOS_MAX_BILLING_PER_USER;
	if (!xstrcasecmp(reason, "QOSMaxBillingMinutesPerJob"))
		return WAIT_QOS_MAX_BILLING_MINS_PER_JOB;
	if (!xstrcasecmp(reason, "MaxBillingPerAccount"))
		return WAIT_QOS_MAX_BILLING_PER_ACCT;
	if (!xstrcasecmp(reason, "QOSMinBilling"))
		return WAIT_QOS_MIN_BILLING;
	if (!xstrcasecmp(reason, "ReservationDeleted"))
		return WAIT_RESV_DELETED;

	return NO_VAL;
}

/* If the job is held up by a QOS GRP limit return true else return false. */
extern bool job_state_qos_grp_limit(enum job_state_reason state_reason)
{
	if ((state_reason >= WAIT_QOS_GRP_CPU &&
	     state_reason <= WAIT_QOS_GRP_WALL) ||
	    (state_reason == WAIT_QOS_GRP_MEM_MIN) ||
	    (state_reason == WAIT_QOS_GRP_MEM_RUN_MIN) ||
	    (state_reason >= WAIT_QOS_GRP_ENERGY &&
	     state_reason <= WAIT_QOS_GRP_ENERGY_RUN_MIN) ||
	    (state_reason == WAIT_QOS_GRP_NODE_MIN) ||
	    (state_reason == WAIT_QOS_GRP_NODE_RUN_MIN) ||
	    (state_reason >= WAIT_QOS_GRP_GRES &&
	     state_reason <= WAIT_QOS_GRP_GRES_RUN_MIN) ||
	    (state_reason >= WAIT_QOS_GRP_LIC &&
	     state_reason <= WAIT_QOS_GRP_LIC_RUN_MIN) ||
	    (state_reason >= WAIT_QOS_GRP_BB &&
	     state_reason <= WAIT_QOS_GRP_BB_RUN_MIN) ||
	    (state_reason >= WAIT_QOS_GRP_BILLING &&
	     state_reason <= WAIT_QOS_GRP_BILLING_RUN_MIN))
		return true;
	return false;
}

extern void slurm_free_get_kvs_msg(kvs_get_msg_t *msg)
{
	if (msg) {
		xfree(msg->hostname);
		xfree(msg);
	}
}

extern void slurm_free_kvs_comm_set(kvs_comm_set_t *msg)
{
	int i, j;

	if (msg) {
		if (msg->kvs_host_ptr) {
			for (i = 0; i < msg->host_cnt; i++)
				xfree(msg->kvs_host_ptr[i].hostname);
			xfree(msg->kvs_host_ptr);
		}
		if (msg->kvs_comm_ptr) {
			for (i = 0; i < msg->kvs_comm_recs; i++) {
				if (!msg->kvs_comm_ptr[i])
					continue;

				xfree(msg->kvs_comm_ptr[i]->kvs_name);
				for (j = 0; j < msg->kvs_comm_ptr[i]->kvs_cnt;
				     j++) {
					xfree(msg->kvs_comm_ptr[i]->
					      kvs_keys[j]);
					xfree(msg->kvs_comm_ptr[i]->
					      kvs_values[j]);
				}
				xfree(msg->kvs_comm_ptr[i]->kvs_keys);
				xfree(msg->kvs_comm_ptr[i]->kvs_values);
			}
			xfree(msg->kvs_comm_ptr);
		}
		xfree(msg);
	}
}

extern void slurm_free_will_run_response_msg(will_run_response_msg_t *msg)
{
	if (msg) {
		xfree(msg->job_submit_user_msg);
		xfree(msg->node_list);
		xfree(msg->part_name);
		FREE_NULL_LIST(msg->preemptee_job_id);
		xfree(msg);
	}
}

extern void slurm_free_forward_data_msg(forward_data_msg_t *msg)
{
	if (msg) {
		xfree(msg->address);
		xfree(msg->data);
		xfree(msg);
	}
}

extern void slurm_free_ping_slurmd_resp(ping_slurmd_resp_msg_t *msg)
{
	xfree(msg);
}

/*
 * structured as a static lookup table, which allows this
 * to be thread safe while avoiding any heap allocation
 */
extern const char *preempt_mode_string(uint16_t preempt_mode)
{
	if (preempt_mode == PREEMPT_MODE_OFF)
		return "OFF";
	if (preempt_mode == PREEMPT_MODE_GANG)
		return "GANG";
	if (preempt_mode == PREEMPT_MODE_WITHIN)
		return "WITHIN";

	if (preempt_mode & PREEMPT_MODE_GANG) {
		preempt_mode &= (~PREEMPT_MODE_GANG);
		if (preempt_mode == PREEMPT_MODE_CANCEL)
			return "GANG,CANCEL";
		else if (preempt_mode == PREEMPT_MODE_REQUEUE)
			return "GANG,REQUEUE";
		else if (preempt_mode == PREEMPT_MODE_SUSPEND)
			return "GANG,SUSPEND";
		return "GANG,UNKNOWN";
	} else if (preempt_mode & PREEMPT_MODE_WITHIN) {
		preempt_mode &= (~PREEMPT_MODE_WITHIN);
		if (preempt_mode == PREEMPT_MODE_CANCEL)
			return "WITHIN,CANCEL";
		else if (preempt_mode == PREEMPT_MODE_REQUEUE)
			return "WITHIN,REQUEUE";
		else if (preempt_mode == PREEMPT_MODE_SUSPEND)
			return "WITHIN,SUSPEND";
		return "WITHIN,UNKNOWN";
	} else {
		if (preempt_mode == PREEMPT_MODE_CANCEL)
			return "CANCEL";
		else if (preempt_mode == PREEMPT_MODE_REQUEUE)
			return "REQUEUE";
		else if (preempt_mode == PREEMPT_MODE_SUSPEND)
			return "SUSPEND";
	}

	return "UNKNOWN";
}

extern uint16_t preempt_mode_num(const char *preempt_mode)
{
	uint16_t mode_num = 0;
	int preempt_modes = 0;
	char *tmp_str, *last = NULL, *tok;

	if (preempt_mode == NULL)
		return mode_num;

	tmp_str = xstrdup(preempt_mode);
	tok = strtok_r(tmp_str, ",", &last);
	while (tok) {
		if (xstrcasecmp(tok, "gang") == 0) {
			mode_num |= PREEMPT_MODE_GANG;
		} else if (!xstrcasecmp(tok, "within")) {
			mode_num |= PREEMPT_MODE_WITHIN;
		} else if ((xstrcasecmp(tok, "off") == 0)
			   || (xstrcasecmp(tok, "cluster") == 0)) {
			mode_num += PREEMPT_MODE_OFF;
			preempt_modes++;
		} else if (xstrcasecmp(tok, "cancel") == 0) {
			mode_num += PREEMPT_MODE_CANCEL;
			preempt_modes++;
		} else if (xstrcasecmp(tok, "requeue") == 0) {
			mode_num += PREEMPT_MODE_REQUEUE;
			preempt_modes++;
		} else if ((xstrcasecmp(tok, "on") == 0) ||
			   (xstrcasecmp(tok, "suspend") == 0)) {
			mode_num += PREEMPT_MODE_SUSPEND;
			preempt_modes++;
		} else {
			preempt_modes = 0;
			mode_num = NO_VAL16;
			break;
		}
		tok = strtok_r(NULL, ",", &last);
	}
	xfree(tmp_str);

	if (preempt_modes > 1) {
		/*
		 * Only one mode value may be set, optionally combined with
		 * GANG or WITHIN.
		 */
		mode_num = NO_VAL16;
	} else if ((mode_num & PREEMPT_MODE_GANG) &&
		   (mode_num & PREEMPT_MODE_WITHIN)) {
		/* "GANG,WITHIN" is an invalid combination */
		mode_num = NO_VAL16;
	}

	return mode_num;
}

/* Convert log level number to equivalent string */
extern char *log_num2string(uint16_t inx)
{
	switch (inx) {
	case LOG_LEVEL_QUIET:
		return "quiet";
	case LOG_LEVEL_FATAL:
		return "fatal";
	case LOG_LEVEL_ERROR:
		return "error";
	case LOG_LEVEL_INFO:
		return "info";
	case LOG_LEVEL_VERBOSE:
		return "verbose";
	case LOG_LEVEL_DEBUG:
		return "debug";
	case LOG_LEVEL_DEBUG2:
		return "debug2";
	case LOG_LEVEL_DEBUG3:
		return "debug3";
	case LOG_LEVEL_DEBUG4:
		return "debug4";
	case LOG_LEVEL_DEBUG5:
		return "debug5";
	case LOG_LEVEL_END:
		/*
		 * "(null)" is printed through 'scontrol show config' to
		 * to indicate a given value has not been set. Convert
		 * LOG_LEVEL_END to "(null)" to indicate a given logging
		 * channel is disabled, rather than printing "unknown".
		 */
		return "(null)";
	default:
		return "unknown";
	}
}

/* Convert log level string to equivalent number */
extern uint16_t log_string2num(const char *name)
{
	if (name == NULL)
		return NO_VAL16;

	if ((name[0] >= '0') && (name[0] <= '9'))
		return (uint16_t) atoi(name);

	if (!xstrcasecmp(name, "quiet"))
		return (uint16_t) 0;
	if (!xstrcasecmp(name, "fatal"))
		return (uint16_t) 1;
	if (!xstrcasecmp(name, "error"))
		return (uint16_t) 2;
	if (!xstrcasecmp(name, "info"))
		return (uint16_t) 3;
	if (!xstrcasecmp(name, "verbose"))
		return (uint16_t) 4;
	if (!xstrcasecmp(name, "debug"))
		return (uint16_t) 5;
	if (!xstrcasecmp(name, "debug2"))
		return (uint16_t) 6;
	if (!xstrcasecmp(name, "debug3"))
		return (uint16_t) 7;
	if (!xstrcasecmp(name, "debug4"))
		return (uint16_t) 8;
	if (!xstrcasecmp(name, "debug5"))
		return (uint16_t) 9;

	return NO_VAL16;
}

extern char *job_share_string(uint16_t shared)
{
	if (shared == JOB_SHARED_NONE)
		return "NO";
	else if (shared == JOB_SHARED_OK)
		return "YES";
	else if (shared == JOB_SHARED_USER)
		return "USER";
	else if (shared == JOB_SHARED_MCS)
		return "MCS";
	else
		return "OK";
}

extern char *job_state_string(uint32_t inx)
{
	/* Process JOB_STATE_FLAGS */
	if (inx & JOB_COMPLETING)
		return "COMPLETING";
	if (inx & JOB_STAGE_OUT)
		return "STAGE_OUT";
	if (inx & JOB_CONFIGURING)
		return "CONFIGURING";
	if (inx & JOB_RESIZING)
		return "RESIZING";
	if (inx & JOB_REQUEUE)
		return "REQUEUED";
	if (inx & JOB_REQUEUE_FED)
		return "REQUEUE_FED";
	if (inx & JOB_REQUEUE_HOLD)
		return "REQUEUE_HOLD";
	if (inx & JOB_SPECIAL_EXIT)
		return "SPECIAL_EXIT";
	if (inx & JOB_STOPPED)
		return "STOPPED";
	if (inx & JOB_REVOKED)
		return "REVOKED";
	if (inx & JOB_RESV_DEL_HOLD)
		return "RESV_DEL_HOLD";
	if (inx & JOB_SIGNALING)
		return "SIGNALING";

	/* Process JOB_STATE_BASE */
	switch (inx & JOB_STATE_BASE) {
	case JOB_PENDING:
		return "PENDING";
	case JOB_RUNNING:
		return "RUNNING";
	case JOB_SUSPENDED:
		return "SUSPENDED";
	case JOB_COMPLETE:
		return "COMPLETED";
	case JOB_CANCELLED:
		return "CANCELLED";
	case JOB_FAILED:
		return "FAILED";
	case JOB_TIMEOUT:
		return "TIMEOUT";
	case JOB_NODE_FAIL:
		return "NODE_FAIL";
	case JOB_PREEMPTED:
		return "PREEMPTED";
	case JOB_BOOT_FAIL:
		return "BOOT_FAIL";
	case JOB_DEADLINE:
		return "DEADLINE";
	case JOB_OOM:
		return "OUT_OF_MEMORY";
	default:
		return "?";
	}
}

extern char *job_state_string_compact(uint32_t inx)
{
	/* Process JOB_STATE_FLAGS */
	if (inx & JOB_COMPLETING)
		return "CG";
	if (inx & JOB_STAGE_OUT)
		return "SO";
	if (inx & JOB_CONFIGURING)
		return "CF";
	if (inx & JOB_RESIZING)
		return "RS";
	if (inx & JOB_REQUEUE)
		return "RQ";
	if (inx & JOB_REQUEUE_FED)
		return "RF";
	if (inx & JOB_REQUEUE_HOLD)
		return "RH";
	if (inx & JOB_SPECIAL_EXIT)
		return "SE";
	if (inx & JOB_STOPPED)
		return "ST";
	if (inx & JOB_REVOKED)
		return "RV";
	if (inx & JOB_RESV_DEL_HOLD)
		return "RD";
	if (inx & JOB_SIGNALING)
		return "SI";

	/* Process JOB_STATE_BASE */
	switch (inx & JOB_STATE_BASE) {
	case JOB_PENDING:
		return "PD";
	case JOB_RUNNING:
		return "R";
	case JOB_SUSPENDED:
		return "S";
	case JOB_COMPLETE:
		return "CD";
	case JOB_CANCELLED:
		return "CA";
	case JOB_FAILED:
		return "F";
	case JOB_TIMEOUT:
		return "TO";
	case JOB_NODE_FAIL:
		return "NF";
	case JOB_PREEMPTED:
		return "PR";
	case JOB_BOOT_FAIL:
		return "BF";
	case JOB_DEADLINE:
		return "DL";
	case JOB_OOM:
		return "OOM";
	default:
		return "?";
	}
}

/*
 * job_state_string_complete - build a string describing the job state
 *
 * IN: state - job state
 * RET string representation of the job state;
 * NOTE: the caller must call xfree() on the RET value to free memory
 */
extern char *job_state_string_complete(uint32_t state)
{
	/* Malloc space ahead of time to avoid realloc inside of xstrcat. */
	char *state_str = xmalloc(100);

	/* Process JOB_STATE_BASE */
	switch (state & JOB_STATE_BASE) {
	case JOB_PENDING:
		xstrcat(state_str, "PENDING");
		break;
	case JOB_RUNNING:
		xstrcat(state_str, "RUNNING");
		break;
	case JOB_SUSPENDED:
		xstrcat(state_str, "SUSPENDED");
		break;
	case JOB_COMPLETE:
		xstrcat(state_str, "COMPLETED");
		break;
	case JOB_CANCELLED:
		xstrcat(state_str, "CANCELLED");
		break;
	case JOB_FAILED:
		xstrcat(state_str, "FAILED");
		break;
	case JOB_TIMEOUT:
		xstrcat(state_str, "TIMEOUT");
		break;
	case JOB_NODE_FAIL:
		xstrcat(state_str, "NODE_FAIL");
		break;
	case JOB_PREEMPTED:
		xstrcat(state_str, "PREEMPTED");
		break;
	case JOB_BOOT_FAIL:
		xstrcat(state_str, "BOOT_FAIL");
		break;
	case JOB_DEADLINE:
		xstrcat(state_str, "DEADLINE");
		break;
	case JOB_OOM:
		xstrcat(state_str, "OUT_OF_MEMORY");
		break;
	default:
		xstrcat(state_str, "?");
		break;
	}

	/* Process JOB_STATE_FLAGS */
	if (state & JOB_LAUNCH_FAILED)
		xstrcat(state_str, ",LAUNCH_FAILED");
	if (state & JOB_UPDATE_DB)
		xstrcat(state_str, ",UPDATE_DB");
	if (state & JOB_COMPLETING)
		xstrcat(state_str, ",COMPLETING");
	if (state & JOB_CONFIGURING)
		xstrcat(state_str, ",CONFIGURING");
	if (state & JOB_POWER_UP_NODE)
		xstrcat(state_str, ",POWER_UP_NODE");
	if (state & JOB_RECONFIG_FAIL)
		xstrcat(state_str, ",RECONFIG_FAIL");
	if (state & JOB_RESIZING)
		xstrcat(state_str, ",RESIZING");
	if (state & JOB_REQUEUE)
		xstrcat(state_str, ",REQUEUED");
	if (state & JOB_REQUEUE_FED)
		xstrcat(state_str, ",REQUEUE_FED");
	if (state & JOB_REQUEUE_HOLD)
		xstrcat(state_str, ",REQUEUE_HOLD");
	if (state & JOB_SPECIAL_EXIT)
		xstrcat(state_str, ",SPECIAL_EXIT");
	if (state & JOB_STOPPED)
		xstrcat(state_str, ",STOPPED");
	if (state & JOB_REVOKED)
		xstrcat(state_str, ",REVOKED");
	if (state & JOB_RESV_DEL_HOLD)
		xstrcat(state_str, ",RESV_DEL_HOLD");
	if (state & JOB_SIGNALING)
		xstrcat(state_str, ",SIGNALING");
	if (state & JOB_STAGE_OUT)
		xstrcat(state_str, ",STAGE_OUT");

	return state_str;
}

static bool _job_name_test(uint32_t state_num, const char *state_name)
{
	if (!xstrcasecmp(state_name, job_state_string(state_num)) ||
	    !xstrcasecmp(state_name, job_state_string_compact(state_num))) {
		return true;
	}
	return false;
}

extern uint32_t job_state_num(const char *state_name)
{
	uint32_t i;

	for (i = 0; i < JOB_END; i++) {
		if (_job_name_test(i, state_name))
			return i;
	}

	if (_job_name_test(JOB_COMPLETING, state_name))
		return JOB_COMPLETING;
	if (_job_name_test(JOB_CONFIGURING, state_name))
		return JOB_CONFIGURING;
	if (_job_name_test(JOB_RESIZING, state_name))
		return JOB_RESIZING;
	if (_job_name_test(JOB_RESV_DEL_HOLD, state_name))
		return JOB_RESV_DEL_HOLD;
	if (_job_name_test(JOB_REQUEUE, state_name))
		return JOB_REQUEUE;
	if (_job_name_test(JOB_REQUEUE_FED, state_name))
		return JOB_REQUEUE_FED;
	if (_job_name_test(JOB_REQUEUE_HOLD, state_name))
		return JOB_REQUEUE_HOLD;
	if (_job_name_test(JOB_REVOKED, state_name))
		return JOB_REVOKED;
	if (_job_name_test(JOB_SIGNALING, state_name))
		return JOB_SIGNALING;
	if (_job_name_test(JOB_SPECIAL_EXIT, state_name))
		return JOB_SPECIAL_EXIT;
	if (_job_name_test(JOB_STAGE_OUT, state_name))
		return JOB_STAGE_OUT;
	if (_job_name_test(JOB_STOPPED, state_name))
		return JOB_STOPPED;

	return NO_VAL;
}

extern char *trigger_res_type(uint16_t res_type)
{
	if      (res_type == TRIGGER_RES_TYPE_JOB)
		return "job";
	else if (res_type == TRIGGER_RES_TYPE_NODE)
		return "node";
	else if (res_type == TRIGGER_RES_TYPE_SLURMCTLD)
		return "slurmctld";
	else if (res_type == TRIGGER_RES_TYPE_SLURMDBD)
		return "slurmdbd";
	else if (res_type == TRIGGER_RES_TYPE_DATABASE)
		return "database";
	else if (res_type == TRIGGER_RES_TYPE_FRONT_END)
		return "front_end";
	else if (res_type == TRIGGER_RES_TYPE_OTHER)
		return "other";
	else
		return "unknown";
}

/* Convert HealthCheckNodeState numeric value to a string.
 * Caller must xfree() the return value */
extern char *health_check_node_state_str(uint32_t node_state)
{
	char *state_str = NULL;

	if (node_state & HEALTH_CHECK_CYCLE)
		state_str = xstrdup("CYCLE");
	else
		state_str = xstrdup("");

	if ((node_state & HEALTH_CHECK_NODE_ANY) == HEALTH_CHECK_NODE_ANY) {
		if (state_str[0])
			xstrcat(state_str, ",");
		xstrcat(state_str, "ANY");
		return state_str;
	}

	if (node_state & HEALTH_CHECK_NODE_IDLE) {
		if (state_str[0])
			xstrcat(state_str, ",");
		xstrcat(state_str, "IDLE");
	}
	if (node_state & HEALTH_CHECK_NODE_ALLOC) {
		if (state_str[0])
			xstrcat(state_str, ",");
		xstrcat(state_str, "ALLOC");
	}
	if (node_state & HEALTH_CHECK_NODE_MIXED) {
		if (state_str[0])
			xstrcat(state_str, ",");
		xstrcat(state_str, "MIXED");
	}

	return state_str;
}

extern char *trigger_type(uint32_t trig_type)
{
	if      (trig_type == TRIGGER_TYPE_UP)
		return "up";
	else if (trig_type == TRIGGER_TYPE_DOWN)
		return "down";
	else if (trig_type == TRIGGER_TYPE_DRAINED)
		return "drained";
	else if (trig_type == TRIGGER_TYPE_FAIL)
		return "fail";
	else if (trig_type == TRIGGER_TYPE_IDLE)
		return "idle";
	else if (trig_type == TRIGGER_TYPE_TIME)
		return "time";
	else if (trig_type == TRIGGER_TYPE_FINI)
		return "fini";
	else if (trig_type == TRIGGER_TYPE_RECONFIG)
		return "reconfig";
	else if (trig_type == TRIGGER_TYPE_PRI_CTLD_FAIL)
		return "primary_slurmctld_failure";
	else if (trig_type == TRIGGER_TYPE_PRI_CTLD_RES_OP)
		return "primary_slurmctld_resumed_operation";
	else if (trig_type == TRIGGER_TYPE_PRI_CTLD_RES_CTRL)
		return "primary_slurmctld_resumed_control";
	else if (trig_type == TRIGGER_TYPE_PRI_CTLD_ACCT_FULL)
		return "primary_slurmctld_acct_buffer_full";
	else if (trig_type == TRIGGER_TYPE_BU_CTLD_FAIL)
		return "backup_slurmctld_failure";
	else if (trig_type == TRIGGER_TYPE_BU_CTLD_RES_OP)
		return "backup_slurmctld_resumed_operation";
	else if (trig_type == TRIGGER_TYPE_BU_CTLD_AS_CTRL)
		return "backup_slurmctld_assumed_control";
	else if (trig_type == TRIGGER_TYPE_PRI_DBD_FAIL)
		return "primary_slurmdbd_failure";
	else if (trig_type == TRIGGER_TYPE_PRI_DBD_RES_OP)
		return "primary_slurmdbd_resumed_operation";
	else if (trig_type == TRIGGER_TYPE_PRI_DB_FAIL)
		return "primary_database_failure";
	else if (trig_type == TRIGGER_TYPE_PRI_DB_RES_OP)
		return "primary_database_resumed_operation";
	else if (trig_type == TRIGGER_TYPE_BURST_BUFFER)
		return "burst_buffer";
	else
		return "unknown";
}

/* user needs to xfree return value */
extern char *reservation_flags_string(reserve_info_t * resv_ptr)
{
	char *flag_str = xstrdup("");
	uint64_t flags = resv_ptr->flags;

	if (flags & RESERVE_FLAG_MAINT)
		xstrcat(flag_str, "MAINT");
	if (flags & RESERVE_FLAG_NO_MAINT) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_MAINT");
	}
	if (flags & RESERVE_FLAG_FLEX) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "FLEX");
	}
	if (flags & RESERVE_FLAG_OVERLAP) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "OVERLAP");
	}
	if (flags & RESERVE_FLAG_IGN_JOBS) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "IGNORE_JOBS");
	}
	if (flags & RESERVE_FLAG_HOURLY) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "HOURLY");
	}
	if (flags & RESERVE_FLAG_NO_HOURLY) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_HOURLY");
	}
	if (flags & RESERVE_FLAG_DAILY) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "DAILY");
	}
	if (flags & RESERVE_FLAG_NO_DAILY) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_DAILY");
	}
	if (flags & RESERVE_FLAG_WEEKDAY) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "WEEKDAY");
	}
	if (flags & RESERVE_FLAG_WEEKEND) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "WEEKEND");
	}
	if (flags & RESERVE_FLAG_WEEKLY) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "WEEKLY");
	}
	if (flags & RESERVE_FLAG_NO_WEEKLY) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_WEEKLY");
	}
	if (flags & RESERVE_FLAG_SPEC_NODES) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "SPEC_NODES");
	}
	if (flags & RESERVE_FLAG_ALL_NODES) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "ALL_NODES");
	}
	if (flags & RESERVE_FLAG_ANY_NODES) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "ANY_NODES");
	}
	if (flags & RESERVE_FLAG_NO_ANY_NODES) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_ANY_NODES");
	}
	if (flags & RESERVE_FLAG_STATIC) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "STATIC");
	}
	if (flags & RESERVE_FLAG_NO_STATIC) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_STATIC");
	}
	if (flags & RESERVE_FLAG_PART_NODES) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "PART_NODES");
	}
	if (flags & RESERVE_FLAG_NO_PART_NODES) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_PART_NODES");
	}
	if (flags & RESERVE_FLAG_FIRST_CORES) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "FIRST_CORES");
	}
	if (flags & RESERVE_FLAG_TIME_FLOAT) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "TIME_FLOAT");
	}
	if (flags & RESERVE_FLAG_REPLACE) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "REPLACE");
	}
	if (flags & RESERVE_FLAG_REPLACE_DOWN) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "REPLACE_DOWN");
	}
	if (flags & RESERVE_FLAG_PURGE_COMP) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		if (resv_ptr->purge_comp_time) {
			char tmp_pct[40];
			secs2time_str(resv_ptr->purge_comp_time,
				      tmp_pct, sizeof(tmp_pct));
			xstrfmtcat(flag_str, "PURGE_COMP=%s", tmp_pct);
		} else
			xstrcat(flag_str, "PURGE_COMP");
	}
	if (flags & RESERVE_FLAG_NO_HOLD_JOBS) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_HOLD_JOBS_AFTER_END");
	}
	if (flags & RESERVE_FLAG_MAGNETIC) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "MAGNETIC");
	}
	if (flags & RESERVE_FLAG_NO_MAGNETIC) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_MAGNETIC");
	}


	return flag_str;
}

/* user needs to xfree return value */
extern char *priority_flags_string(uint16_t priority_flags)
{
	char *flag_str = xstrdup("");

	if (priority_flags & PRIORITY_FLAGS_ACCRUE_ALWAYS)
		xstrcat(flag_str, "ACCRUE_ALWAYS");
	if (priority_flags & PRIORITY_FLAGS_SIZE_RELATIVE) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "SMALL_RELATIVE_TO_TIME");
	}
	if (priority_flags & PRIORITY_FLAGS_CALCULATE_RUNNING) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "CALCULATE_RUNNING");
	}
	if (priority_flags & PRIORITY_FLAGS_DEPTH_OBLIVIOUS) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "DEPTH_OBLIVIOUS");
	}
	if (!(priority_flags & PRIORITY_FLAGS_FAIR_TREE)) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_FAIR_TREE");
	}
	if (priority_flags & PRIORITY_FLAGS_INCR_ONLY) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "INCR_ONLY");
	}
	if (priority_flags & PRIORITY_FLAGS_MAX_TRES) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "MAX_TRES");
	}
	if (priority_flags & (PRIORITY_FLAGS_NO_NORMAL_ASSOC |
			      PRIORITY_FLAGS_NO_NORMAL_PART  |
			      PRIORITY_FLAGS_NO_NORMAL_QOS   |
			      PRIORITY_FLAGS_NO_NORMAL_TRES)) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_NORMAL_ALL");
	} else {
		if (priority_flags & PRIORITY_FLAGS_NO_NORMAL_ASSOC) {
			if (flag_str[0])
				xstrcat(flag_str, ",");
			xstrcat(flag_str, "NO_NORMAL_ASSOC");
		}
		if (priority_flags & PRIORITY_FLAGS_NO_NORMAL_PART) {
			if (flag_str[0])
				xstrcat(flag_str, ",");
			xstrcat(flag_str, "NO_NORMAL_PART");
		}
		if (priority_flags & PRIORITY_FLAGS_NO_NORMAL_QOS) {
			if (flag_str[0])
				xstrcat(flag_str, ",");
			xstrcat(flag_str, "NO_NORMAL_QOS");
		}
		if (priority_flags & PRIORITY_FLAGS_NO_NORMAL_TRES) {
			if (flag_str[0])
				xstrcat(flag_str, ",");
			xstrcat(flag_str, "NO_NORMAL_TRES");
		}
	}

	return flag_str;
}

/* Translate a burst buffer numeric value to its equivalant state string */
extern char *bb_state_string(uint16_t state)
{
	static char buf[16];

	if (state == BB_STATE_PENDING)
		return "pending";
	if (state == BB_STATE_ALLOCATING)
		return "allocating";
	if (state == BB_STATE_ALLOCATED)
		return "allocated";
	if (state == BB_STATE_DELETING)
		return "deleting";
	if (state == BB_STATE_DELETED)
		return "deleted";
	if (state == BB_STATE_STAGING_IN)
		return "staging-in";
	if (state == BB_STATE_STAGED_IN)
		return "staged-in";
	if (state == BB_STATE_PRE_RUN)
		return "pre-run";
	if (state == BB_STATE_ALLOC_REVOKE)
		return "alloc-revoke";
	if (state == BB_STATE_RUNNING)
		return "running";
	if (state == BB_STATE_SUSPEND)
		return "suspended";
	if (state == BB_STATE_POST_RUN)
		return "post-run";
	if (state == BB_STATE_STAGING_OUT)
		return "staging-out";
	if (state == BB_STATE_STAGED_OUT)
		return "staged-out";
	if (state == BB_STATE_TEARDOWN)
		return "teardown";
	if (state == BB_STATE_TEARDOWN_FAIL)
		return "teardown-fail";
	if (state == BB_STATE_COMPLETE)
		return "complete";
	snprintf(buf, sizeof(buf), "%u", state);
	return buf;
}

/* Translate a burst buffer state string to its equivalant numeric value */
extern uint16_t bb_state_num(char *tok)
{
	if (!xstrcasecmp(tok, "pending"))
		return BB_STATE_PENDING;
	if (!xstrcasecmp(tok, "allocating"))
		return BB_STATE_ALLOCATING;
	if (!xstrcasecmp(tok, "allocated"))
		return BB_STATE_ALLOCATED;
	if (!xstrcasecmp(tok, "deleting"))
		return BB_STATE_DELETING;
	if (!xstrcasecmp(tok, "deleted"))
		return BB_STATE_DELETED;
	if (!xstrcasecmp(tok, "staging-in"))
		return BB_STATE_STAGING_IN;
	if (!xstrcasecmp(tok, "staged-in"))
		return BB_STATE_STAGED_IN;
	if (!xstrcasecmp(tok, "pre-run"))
		return BB_STATE_PRE_RUN;
	if (!xstrcasecmp(tok, "alloc-revoke"))
		return BB_STATE_ALLOC_REVOKE;
	if (!xstrcasecmp(tok, "running"))
		return BB_STATE_RUNNING;
	if (!xstrcasecmp(tok, "suspend"))
		return BB_STATE_SUSPEND;
	if (!xstrcasecmp(tok, "post-run"))
		return BB_STATE_POST_RUN;
	if (!xstrcasecmp(tok, "staging-out"))
		return BB_STATE_STAGING_OUT;
	if (!xstrcasecmp(tok, "staged-out"))
		return BB_STATE_STAGED_OUT;
	if (!xstrcasecmp(tok, "teardown"))
		return BB_STATE_TEARDOWN;
	if (!xstrcasecmp(tok, "teardown-fail"))
		return BB_STATE_TEARDOWN_FAIL;
	if (!xstrcasecmp(tok, "complete"))
		return BB_STATE_COMPLETE;
	return 0;
}

extern bool valid_base_state(uint32_t state)
{
	for (int i = 0; i < ARRAY_SIZE(node_states); i++) {
		if (node_states[i].flag == (state & NODE_STATE_BASE))
			return true;
	}
	return false;
}

extern const char *node_state_base_string(uint32_t state)
{
	state &= NODE_STATE_BASE;

	for (int i = 0; i < ARRAY_SIZE(node_states); i++)
		if (node_states[i].flag == state)
			return node_states[i].str;

	return "INVALID";
}

extern const char *node_state_flag_string_single(uint32_t *state)
{
	uint32_t flags = *state & NODE_STATE_FLAGS;

	if (!flags)
		return NULL;

	for (int i = 0; i < ARRAY_SIZE(node_state_flags); i++) {
		if (flags & node_state_flags[i].flag) {
			*state &= ~node_state_flags[i].flag;
			return node_state_flags[i].str;
		}
	}
	/*
	 * clear lowest flag bit, in order to guarantee that flags goes to 0 on
	 * repeated calls. Any uncaught flags are unknown here.
	 */
	*state &= ~(flags & -flags);
	return "?";
}

extern char *node_state_flag_string(uint32_t state)
{
	uint32_t flags = state & NODE_STATE_FLAGS;
	const char *flag_str = NULL;
	char *state_str = NULL;

	while ((flag_str = node_state_flag_string_single(&flags))) {
		xstrfmtcat(state_str, "+%s", flag_str);
	}
	return state_str;
}

extern char *node_state_string_complete(uint32_t state)
{
	char *state_str = NULL, *flags_str = NULL;

	state_str = xstrdup(node_state_base_string(state));
	if ((flags_str = node_state_flag_string(state))) {
		xstrcat(state_str, flags_str);
		xfree(flags_str);
	}

	return state_str;
}

extern char *node_state_string(uint32_t inx)
{
	int  base            = (inx & NODE_STATE_BASE);
	bool comp_flag       = (inx & NODE_STATE_COMPLETING);
	bool drain_flag      = (inx & NODE_STATE_DRAIN);
	bool fail_flag       = (inx & NODE_STATE_FAIL);
	bool maint_flag      = (inx & NODE_STATE_MAINT);
	bool net_flag        = (inx & NODE_STATE_NET);
	bool reboot_flag     = (inx & NODE_STATE_REBOOT_REQUESTED);
	bool reboot_issued_flag = (inx & NODE_STATE_REBOOT_ISSUED);
	bool res_flag        = (inx & NODE_STATE_RES);
	bool resume_flag     = (inx & NODE_RESUME);
	bool no_resp_flag    = (inx & NODE_STATE_NO_RESPOND);
	bool planned_flag    = (inx & NODE_STATE_PLANNED);
	bool powered_down_flag = (inx & NODE_STATE_POWERED_DOWN);
	bool power_up_flag   = (inx & NODE_STATE_POWERING_UP);
	bool powering_down_flag = (inx & NODE_STATE_POWERING_DOWN);
	bool power_down_flag = (inx & NODE_STATE_POWER_DOWN);

	if (inx & NODE_STATE_INVALID_REG)
		return "INVAL";

	if (maint_flag) {
		if (drain_flag ||
		    (base == NODE_STATE_ALLOCATED) ||
		    (base == NODE_STATE_DOWN) ||
		    (base == NODE_STATE_MIXED))
			;
		else if (no_resp_flag)
			return "MAINT*";
		else
			return "MAINT";
	}
	if (reboot_flag || reboot_issued_flag) {
		if ((base == NODE_STATE_ALLOCATED) ||
		    (base == NODE_STATE_MIXED))
			;
		else if (reboot_issued_flag)
			return "REBOOT^";
		else if (no_resp_flag)
			return "REBOOT*";
		else
			return "REBOOT";
	}
	if (drain_flag) {
		if (comp_flag
		    || (base == NODE_STATE_ALLOCATED)
		    || (base == NODE_STATE_MIXED)) {
			if (maint_flag)
				return "DRAINING$";
			if (reboot_issued_flag)
				return "DRAINING^";
			if (reboot_flag)
				return "DRAINING@";
			if (power_up_flag)
				return "DRAINING#";
			if (powering_down_flag)
				return "DRAINING%";
			if (powered_down_flag)
				return "DRAINING~";
			if (power_down_flag)
				return "DRAINING!";
			if (no_resp_flag)
				return "DRAINING*";
			return "DRAINING";
		} else {
			if (maint_flag)
				return "DRAINED$";
			if (reboot_issued_flag)
				return "DRAINED^";
			if (reboot_flag)
				return "DRAINED@";
			if (power_up_flag)
				return "DRAINED#";
			if (powering_down_flag)
				return "DRAINED%";
			if (powered_down_flag)
				return "DRAINED~";
			if (power_down_flag)
				return "DRAINED!";
			if (no_resp_flag)
				return "DRAINED*";
			return "DRAINED";
		}
	}
	if (fail_flag) {
		if (comp_flag || (base == NODE_STATE_ALLOCATED)) {
			if (no_resp_flag)
				return "FAILING*";
			return "FAILING";
		} else {
			if (no_resp_flag)
				return "FAIL*";
			return "FAIL";
		}
	}

	if (inx == NODE_STATE_REBOOT_ISSUED)
		return "REBOOT_ISSUED";
	if (inx == NODE_STATE_REBOOT_CANCEL)
		return "CANCEL_REBOOT";
	if (inx == NODE_STATE_CLOUD)
		return "CLOUD";
	if (inx == NODE_STATE_POWER_DOWN)
		return "POWER_DOWN";
	if (inx == NODE_STATE_POWER_UP)
		return "POWER_UP";
	if (inx == NODE_STATE_POWERING_DOWN)
		return "POWERING_DOWN";
	if (inx == NODE_STATE_POWERED_DOWN)
		return "POWERED_DOWN";
	if (inx == NODE_STATE_POWERING_UP)
		return "POWERING_UP";
	if (base == NODE_STATE_DOWN) {
		if (maint_flag)
			return "DOWN$";
		if (reboot_issued_flag)
			return "DOWN^";
		if (reboot_flag)
			return "DOWN@";
		if (power_up_flag)
			return "DOWN#";
		if (powering_down_flag)
			return "DOWN%";
		if (powered_down_flag)
			return "DOWN~";
		if (power_down_flag)
			return "DOWN!";
		if (no_resp_flag)
			return "DOWN*";
		return "DOWN";
	}

	if (base == NODE_STATE_ALLOCATED) {
		if (maint_flag)
			return "ALLOCATED$";
		if (reboot_issued_flag)
			return "ALLOCATED^";
		if (reboot_flag)
			return "ALLOCATED@";
		if (power_up_flag)
			return "ALLOCATED#";
		if (powering_down_flag)
			return "ALLOCATED%";
		if (powered_down_flag)
			return "ALLOCATED~";
		if (power_down_flag)
			return "ALLOCATED!";
		if (no_resp_flag)
			return "ALLOCATED*";
		if (comp_flag)
			return "ALLOCATED+";
		return "ALLOCATED";
	}
	if (comp_flag) {
		if (maint_flag)
			return "COMPLETING$";
		if (reboot_issued_flag)
			return "COMPLETING^";
		if (reboot_flag)
			return "COMPLETING@";
		if (power_up_flag)
			return "COMPLETING#";
		if (powering_down_flag)
			return "COMPLETING%";
		if (powered_down_flag)
			return "COMPLETING~";
		if (power_down_flag)
			return "COMPLETING!";
		if (no_resp_flag)
			return "COMPLETING*";
		return "COMPLETING";
	}
	if (base == NODE_STATE_IDLE) {
		if (maint_flag)
			return "IDLE$";
		if (reboot_issued_flag)
			return "IDLE^";
		if (reboot_flag)
			return "IDLE@";
		if (power_up_flag)
			return "IDLE#";
		if (powering_down_flag)
			return "IDLE%";
		if (powered_down_flag)
			return "IDLE~";
		if (power_down_flag)
			return "IDLE!";
		if (no_resp_flag)
			return "IDLE*";
		if (net_flag)
			return "PERFCTRS";
		if (res_flag)
			return "RESERVED";
		if (planned_flag)
			return "PLANNED";
		return "IDLE";
	}
	if (base == NODE_STATE_MIXED) {
		if (maint_flag)
			return "MIXED$";
		if (reboot_issued_flag)
			return "MIXED^";
		if (reboot_flag)
			return "MIXED@";
		if (power_up_flag)
			return "MIXED#";
		if (powering_down_flag)
			return "MIXED%";
		if (powered_down_flag)
			return "MIXED~";
		if (power_down_flag)
			return "MIXED!";
		if (no_resp_flag)
			return "MIXED*";
		if (planned_flag)
			return "MIXED-";
		return "MIXED";
	}
	if (base == NODE_STATE_FUTURE) {
		if (maint_flag)
			return "FUTURE$";
		if (reboot_issued_flag)
			return "FUTURE^";
		if (reboot_flag)
			return "FUTURE@";
		if (power_up_flag)
			return "FUTURE#";
		if (powering_down_flag)
			return "FUTURE%";
		if (powered_down_flag)
			return "FUTURE~";
		if (power_down_flag)
			return "FUTURE!";
		if (no_resp_flag)
			return "FUTURE*";
		return "FUTURE";
	}
	if (resume_flag)
		return "RESUME";
	if (base == NODE_STATE_UNKNOWN) {
		if (no_resp_flag)
			return "UNKNOWN*";
		return "UNKNOWN";
	}
	return "?";
}

extern char *node_state_string_compact(uint32_t inx)
{
	bool comp_flag       = (inx & NODE_STATE_COMPLETING);
	bool drain_flag      = (inx & NODE_STATE_DRAIN);
	bool fail_flag       = (inx & NODE_STATE_FAIL);
	bool maint_flag      = (inx & NODE_STATE_MAINT);
	bool net_flag        = (inx & NODE_STATE_NET);
	bool reboot_flag     = (inx & NODE_STATE_REBOOT_REQUESTED);
	bool reboot_issued_flag = (inx & NODE_STATE_REBOOT_ISSUED);
	bool res_flag        = (inx & NODE_STATE_RES);
	bool resume_flag     = (inx & NODE_RESUME);
	bool no_resp_flag    = (inx & NODE_STATE_NO_RESPOND);
	bool planned_flag    = (inx & NODE_STATE_PLANNED);
	bool powered_down_flag = (inx & NODE_STATE_POWERED_DOWN);
	bool power_up_flag   = (inx & NODE_STATE_POWERING_UP);
	bool powering_down_flag = (inx & NODE_STATE_POWERING_DOWN);
	bool power_down_flag = (inx & NODE_STATE_POWER_DOWN);

	if (inx & NODE_STATE_INVALID_REG)
		return "INVAL";

	inx = (inx & NODE_STATE_BASE);

	if (maint_flag) {
		if (drain_flag ||
		    (inx == NODE_STATE_ALLOCATED) ||
		    (inx == NODE_STATE_DOWN) ||
		    (inx == NODE_STATE_MIXED))
			;
		else if (no_resp_flag)
			return "MAINT*";
		else
			return "MAINT";
	}
	if (reboot_flag || reboot_issued_flag) {
		if ((inx == NODE_STATE_ALLOCATED) || (inx == NODE_STATE_MIXED))
			;
		else if (reboot_issued_flag)
			return "BOOT^";
		else if (no_resp_flag)
			return "BOOT*";
		else
			return "BOOT";
	}
	if (drain_flag) {
		if (comp_flag
		    || (inx == NODE_STATE_ALLOCATED)
		    || (inx == NODE_STATE_MIXED)) {
			if (maint_flag)
				return "DRNG$";
			if (reboot_issued_flag)
				return "DRNG^";
			if (reboot_flag)
				return "DRNG@";
			if (power_up_flag)
				return "DRNG#";
			if (powering_down_flag)
				return "DRNG%";
			if (powered_down_flag)
				return "DRNG~";
			if (power_down_flag)
				return "DRNG!";
			if (no_resp_flag)
				return "DRNG*";
			return "DRNG";
		} else {
			if (maint_flag)
				return "DRAIN$";
			if (reboot_issued_flag)
				return "DRAIN^";
			if (reboot_flag)
				return "DRAIN@";
			if (power_up_flag)
				return "DRAIN#";
			if (powering_down_flag)
				return "DRAIN%";
			if (powered_down_flag)
				return "DRAIN~";
			if (power_down_flag)
				return "DRAIN!";
			if (no_resp_flag)
				return "DRAIN*";
			return "DRAIN";
		}
	}
	if (fail_flag) {
		if (comp_flag || (inx == NODE_STATE_ALLOCATED)) {
			if (no_resp_flag)
				return "FAILG*";
			return "FAILG";
		} else {
			if (no_resp_flag)
				return "FAIL*";
			return "FAIL";
		}
	}

	if (inx == NODE_STATE_REBOOT_ISSUED)
		return "BOOT^";
	if (inx == NODE_STATE_REBOOT_CANCEL)
		return "CANC_R";
	if (inx == NODE_STATE_CLOUD)
		return "CLOUD";
	if (inx == NODE_STATE_POWER_DOWN)
		return "POW_DN";
	if (inx == NODE_STATE_POWER_UP)
		return "POW_UP";
	if (inx == NODE_STATE_POWERING_DOWN)
		return "POWRING_DN";
	if (inx == NODE_STATE_POWERED_DOWN)
		return "POWERED_DN";
	if (inx == NODE_STATE_POWERING_UP)
		return "POWERING_UP";
	if (inx == NODE_STATE_DOWN) {
		if (maint_flag)
			return "DOWN$";
		if (reboot_issued_flag)
			return "DOWN^";
		if (reboot_flag)
			return "DOWN@";
		if (power_up_flag)
			return "DOWN#";
		if (powering_down_flag)
			return "DOWN%";
		if (powered_down_flag)
			return "DOWN~";
		if (power_down_flag)
			return "DOWN!";
		if (no_resp_flag)
			return "DOWN*";
		return "DOWN";
	}

	if (inx == NODE_STATE_ALLOCATED) {
		if (maint_flag)
			return "ALLOC$";
		if (reboot_issued_flag)
			return "ALLOC^";
		if (reboot_flag)
			return "ALLOC@";
		if (power_up_flag)
			return "ALLOC#";
		if (powering_down_flag)
			return "ALLOC%";
		if (powered_down_flag)
			return "ALLOC~";
		if (power_down_flag)
			return "ALLOC!";
		if (no_resp_flag)
			return "ALLOC*";
		if (comp_flag)
			return "ALLOC+";
		return "ALLOC";
	}
	if (comp_flag) {
		if (maint_flag)
			return "COMP$";
		if (reboot_issued_flag)
			return "COMP^";
		if (reboot_flag)
			return "COMP@";
		if (power_up_flag)
			return "COMP#";
		if (powering_down_flag)
			return "COMP%";
		if (powered_down_flag)
			return "COMP~";
		if (power_down_flag)
			return "COMP!";
		if (no_resp_flag)
			return "COMP*";
		return "COMP";
	}
	if (inx == NODE_STATE_IDLE) {
		if (maint_flag)
			return "IDLE$";
		if (reboot_issued_flag)
			return "IDLE^";
		if (reboot_flag)
			return "IDLE@";
		if (power_up_flag)
			return "IDLE#";
		if (powering_down_flag)
			return "IDLE%";
		if (powered_down_flag)
			return "IDLE~";
		if (power_down_flag)
			return "IDLE!";
		if (no_resp_flag)
			return "IDLE*";
		if (net_flag)
			return "NPC";
		if (res_flag)
			return "RESV";
		if (planned_flag)
			return "PLND";
		return "IDLE";
	}
	if (inx == NODE_STATE_MIXED) {
		if (maint_flag)
			return "MIX$";
		if (reboot_issued_flag)
			return "MIX^";
		if (reboot_flag)
			return "MIX@";
		if (power_up_flag)
			return "MIX#";
		if (powering_down_flag)
			return "MIX%";
		if (powered_down_flag)
			return "MIX~";
		if (power_down_flag)
			return "MIX!";
		if (no_resp_flag)
			return "MIX*";
		if (planned_flag)
			return "MIX-";
		return "MIX";
	}
	if (inx == NODE_STATE_FUTURE) {
		if (maint_flag)
			return "FUTR$";
		if (reboot_issued_flag)
			return "FUTR^";
		if (reboot_flag)
			return "FUTR@";
		if (power_up_flag)
			return "FUTR#";
		if (powering_down_flag)
			return "FUTR%";
		if (powered_down_flag)
			return "FUTR~";
		if (power_down_flag)
			return "FUTR!";
		if (no_resp_flag)
			return "FUTR*";
		return "FUTR";
	}
	if (resume_flag)
		return "RESM";
	if (inx == NODE_STATE_UNKNOWN) {
		if (no_resp_flag)
			return "UNK*";
		return "UNK";
	}
	return "?";
}

extern uint16_t power_flags_id(const char *power_flags)
{
	char *tmp, *tok, *save_ptr = NULL;
	uint16_t rc = 0;

	if (!power_flags)
		return rc;

	tmp = xstrdup(power_flags);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		if (!xstrcasecmp(tok, "level"))
			rc |= SLURM_POWER_FLAGS_LEVEL;
		else
			error("Ignoring unrecognized power option (%s)", tok);
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);

	return rc;
}

extern char *power_flags_str(uint16_t power_flags)
{
	if (power_flags & SLURM_POWER_FLAGS_LEVEL)
		return "LEVEL";
	return "";
}

extern void private_data_string(uint16_t private_data, char *str, int str_len)
{
	if (str_len > 0)
		str[0] = '\0';
	if (str_len < 69) {
		error("private_data_string: output buffer too small");
		return;
	}

	if (private_data & PRIVATE_DATA_ACCOUNTS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "accounts"); //9 len
	}
	if (private_data & PRIVATE_CLOUD_NODES) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "cloud"); //6 len
	}
	if (private_data & PRIVATE_DATA_EVENTS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "events"); //7 len
	}
	if (private_data & PRIVATE_DATA_JOBS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "jobs"); //5 len
	}
	if (private_data & PRIVATE_DATA_NODES) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "nodes"); //6 len
	}
	if (private_data & PRIVATE_DATA_PARTITIONS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "partitions"); //11 len
	}
	if (private_data & PRIVATE_DATA_RESERVATIONS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "reservations"); //13 len
	}
	if (private_data & PRIVATE_DATA_USAGE) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "usage"); //6 len
	}
	if (private_data & PRIVATE_DATA_USERS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "users"); //6 len
	}

	// total len 69

	if (str[0] == '\0')
		strcat(str, "none");
}

extern void accounting_enforce_string(uint16_t enforce, char *str, int str_len)
{
	if (str_len > 0)
		str[0] = '\0';
	if (str_len < 50) {
		error("enforce: output buffer too small");
		return;
	}

	if (enforce & ACCOUNTING_ENFORCE_ASSOCS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "associations"); //12 len
	}
	if (enforce & ACCOUNTING_ENFORCE_LIMITS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "limits"); //7 len
	}
	if (enforce & ACCOUNTING_ENFORCE_NO_JOBS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "nojobs"); //7 len
	}
	if (enforce & ACCOUNTING_ENFORCE_NO_STEPS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "nosteps"); //8 len
	}
	if (enforce & ACCOUNTING_ENFORCE_QOS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "qos"); //4 len
	}
	if (enforce & ACCOUNTING_ENFORCE_SAFE) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "safe"); //5 len
	}
	if (enforce & ACCOUNTING_ENFORCE_WCKEYS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "wckeys"); //7 len
	}
	// total len 50

	if (str[0] == '\0')
		strcat(str, "none");
}

extern char *cray_nodelist2nids(hostlist_t hl_in, char *nodelist)
{
	hostlist_t hl = hl_in;
	char *nids = NULL, *node_name, *sep = "";
	int i, nid;
	int nid_begin = -1, nid_end = -1;

	if (!nodelist && !hl_in)
		return NULL;

	/* Make hl off nodelist */
	if (!hl_in) {
		hl = hostlist_create(nodelist);
		if (!hl) {
			error("Invalid hostlist: %s", nodelist);
			return NULL;
		}
		//info("input hostlist: %s", nodelist);
		hostlist_uniq(hl);
	}

	while ((node_name = hostlist_shift(hl))) {
		for (i = 0; node_name[i]; i++) {
			if (!isdigit(node_name[i]))
				continue;
			nid = atoi(&node_name[i]);
			if (nid_begin == -1) {
				nid_begin = nid;
				nid_end   = nid;
			} else if (nid == (nid_end + 1)) {
				nid_end   = nid;
			} else {
				if (nid_begin == nid_end) {
					xstrfmtcat(nids, "%s%d", sep,
						   nid_begin);
				} else {
					xstrfmtcat(nids, "%s%d-%d", sep,
						   nid_begin, nid_end);
				}
				nid_begin = nid;
				nid_end   = nid;
				sep = ",";
			}
			break;
		}
		free(node_name);
	}
	if (nid_begin == -1)
		;	/* No data to record */
	else if (nid_begin == nid_end)
		xstrfmtcat(nids, "%s%d", sep, nid_begin);
	else
		xstrfmtcat(nids, "%s%d-%d", sep, nid_begin, nid_end);

	if (!hl_in)
		hostlist_destroy(hl);
	//info("output node IDs: %s", nids);

	return nids;
}

extern void slurm_free_resource_allocation_response_msg_members (
	resource_allocation_response_msg_t * msg)
{
	if (msg) {
		select_g_select_jobinfo_free(msg->select_jobinfo);
		msg->select_jobinfo = NULL;
		xfree(msg->account);
		xfree(msg->alias_list);
		xfree(msg->batch_host);
		xfree(msg->cpus_per_node);
		xfree(msg->cpu_count_reps);
		env_array_free(msg->environment);
		msg->environment = NULL;
		xfree(msg->job_submit_user_msg);
		xfree(msg->node_addr);
		xfree(msg->node_list);
		xfree(msg->partition);
		xfree(msg->qos);
		xfree(msg->resv_name);
		slurmdb_destroy_cluster_rec(msg->working_cluster_rec);
	}
}

/*
 * slurm_free_resource_allocation_response_msg - free slurm resource
 *	allocation response message
 * IN msg - pointer to allocation response message
 * NOTE: buffer is loaded by slurm_allocate_resources
 */
extern void slurm_free_resource_allocation_response_msg (
	resource_allocation_response_msg_t * msg)
{
	slurm_free_resource_allocation_response_msg_members(msg);
	xfree(msg);
}

/*
 * slurm_free_sbcast_cred_msg - free slurm resource allocation response
 *	message including an sbcast credential
 * IN msg - pointer to response message from slurm_sbcast_lookup()
 * NOTE: buffer is loaded by slurm_allocate_resources
 */
extern void slurm_free_sbcast_cred_msg(job_sbcast_cred_msg_t * msg)
{
	if (msg) {
		xfree(msg->node_list);
		delete_sbcast_cred(msg->sbcast_cred);
		xfree(msg);
	}
}

/*
 * slurm_free_job_step_create_response_msg - free slurm
 *	job step create response message
 * IN msg - pointer to job step create response message
 * NOTE: buffer is loaded by slurm_job_step_create
 */
extern void slurm_free_job_step_create_response_msg(
	job_step_create_response_msg_t * msg)
{
	if (msg) {
		xfree(msg->resv_ports);
		slurm_step_layout_destroy(msg->step_layout);
		slurm_cred_destroy(msg->cred);
		if (msg->select_jobinfo)
			select_g_select_jobinfo_free(msg->select_jobinfo);
		if (msg->switch_job)
			switch_g_free_jobinfo(msg->switch_job);

		xfree(msg);
	}

}


/*
 * slurm_free_submit_response_response_msg - free slurm
 *	job submit response message
 * IN msg - pointer to job submit response message
 * NOTE: buffer is loaded by slurm_submit_batch_job
 */
extern void slurm_free_submit_response_response_msg(submit_response_msg_t * msg)
{
	if (msg) {
		xfree(msg->job_submit_user_msg);
		xfree(msg);
	}
}


/*
 * slurm_free_ctl_conf - free slurm control information response message
 * IN msg - pointer to slurm control information response message
 * NOTE: buffer is loaded by slurm_load_jobs
 */
extern void slurm_free_ctl_conf(slurm_ctl_conf_info_msg_t * config_ptr)
{
	if (config_ptr) {
		free_slurm_conf(config_ptr, 0);
		xfree(config_ptr);
	}
}

/*
 * slurm_free_slurmd_status - free slurmd state information
 * IN msg - pointer to slurmd state information
 * NOTE: buffer is loaded by slurm_load_slurmd_status
 */
extern void slurm_free_slurmd_status(slurmd_status_t* slurmd_status_ptr)
{
	if (slurmd_status_ptr) {
		xfree(slurmd_status_ptr->hostname);
		xfree(slurmd_status_ptr->slurmd_logfile);
		xfree(slurmd_status_ptr->step_list);
		xfree(slurmd_status_ptr->version);
		xfree(slurmd_status_ptr);
	}
}

/*
 * slurm_free_job_info - free the job information response message
 * IN msg - pointer to job information response message
 * NOTE: buffer is loaded by slurm_load_job.
 */
extern void slurm_free_job_info_msg(job_info_msg_t * job_buffer_ptr)
{
	if (job_buffer_ptr) {
		if (job_buffer_ptr->job_array) {
			_free_all_job_info(job_buffer_ptr);
			xfree(job_buffer_ptr->job_array);
		}
		xfree(job_buffer_ptr);
	}
}

static void _free_all_job_info(job_info_msg_t *msg)
{
	int i;

	if ((msg == NULL) ||
	    (msg->job_array == NULL))
		return;

	for (i = 0; i < msg->record_count; i++)
		slurm_free_job_info_members (&msg->job_array[i]);
}

/*
 * slurm_free_job_step_info_response_msg - free the job step
 *	information response message
 * IN msg - pointer to job step information response message
 * NOTE: buffer is loaded by slurm_get_job_steps.
 */
extern void slurm_free_job_step_info_response_msg(job_step_info_response_msg_t *
					   msg)
{
	if (msg != NULL) {
		if (msg->job_steps != NULL) {
			_free_all_step_info(msg);
			xfree(msg->job_steps);
		}
		xfree(msg);
	}
}

static void _free_all_step_info (job_step_info_response_msg_t *msg)
{
	int i;

	if ((msg == NULL) ||
	    (msg->job_steps == NULL))
		return;

	for (i = 0; i < msg->job_step_count; i++)
		slurm_free_job_step_info_members (&msg->job_steps[i]);
}

extern void slurm_free_job_step_info_members(job_step_info_t * msg)
{
	if (msg) {
		xfree(msg->cluster);
		xfree(msg->container);
		xfree(msg->tres_per_node);
		xfree(msg->mem_per_tres);
		xfree(msg->name);
		xfree(msg->network);
		xfree(msg->nodes);
		xfree(msg->node_inx);
		xfree(msg->partition);
		xfree(msg->resv_ports);
		select_g_select_jobinfo_free(msg->select_jobinfo);
		msg->select_jobinfo = NULL;
		xfree(msg->srun_host);
		xfree(msg->tres_alloc_str);
		xfree(msg->tres_bind);
		xfree(msg->tres_freq);
		xfree(msg->tres_per_step);
		xfree(msg->tres_per_node);
		xfree(msg->tres_per_socket);
		xfree(msg->tres_per_task);
	}
}

/*
 * slurm_free_front_end_info - free the front_end information response message
 * IN msg - pointer to front_end information response message
 * NOTE: buffer is loaded by slurm_load_front_end.
 */
extern void slurm_free_front_end_info_msg(front_end_info_msg_t * msg)
{
	if (msg) {
		if (msg->front_end_array) {
			_free_all_front_end_info(msg);
			xfree(msg->front_end_array);
		}
		xfree(msg);
	}
}

static void _free_all_front_end_info(front_end_info_msg_t *msg)
{
	int i;

	if ((msg == NULL) || (msg->front_end_array == NULL))
		return;

	for (i = 0; i < msg->record_count; i++)
		slurm_free_front_end_info_members(&msg->front_end_array[i]);
}

extern void slurm_free_front_end_info_members(front_end_info_t * front_end)
{
	if (front_end) {
		xfree(front_end->allow_groups);
		xfree(front_end->allow_users);
		xfree(front_end->deny_groups);
		xfree(front_end->deny_users);
		xfree(front_end->name);
		xfree(front_end->reason);
		xfree(front_end->version);
	}
}

extern void slurm_init_node_info_t(node_info_t *msg, bool clear)
{
	xassert(msg);

	if (clear)
		memset(msg, 0, sizeof(node_info_t));

	msg->next_state = NO_VAL;
}

/*
 * slurm_free_node_info - free the node information response message
 * IN msg - pointer to node information response message
 * NOTE: buffer is loaded by slurm_load_node.
 */
extern void slurm_free_node_info_msg(node_info_msg_t * msg)
{
	if (msg) {
		if (msg->node_array) {
			_free_all_node_info(msg);
			xfree(msg->node_array);
		}
		xfree(msg);
	}
}

static void _free_all_node_info(node_info_msg_t *msg)
{
	int i;

	if ((msg == NULL) || (msg->node_array == NULL))
		return;

	for (i = 0; i < msg->record_count; i++)
		slurm_free_node_info_members(&msg->node_array[i]);
}

extern void slurm_free_node_info_members(node_info_t * node)
{
	if (node) {
		xfree(node->arch);
		xfree(node->cluster_name);
		xfree(node->cpu_spec_list);
		acct_gather_energy_destroy(node->energy);
		ext_sensors_destroy(node->ext_sensors);
		power_mgmt_data_free(node->power);
		xfree(node->features);
		xfree(node->features_act);
		xfree(node->gres);
		xfree(node->gres_drain);
		xfree(node->gres_used);
		xfree(node->mcs_label);
		xfree(node->name);
		xfree(node->node_addr);
		xfree(node->node_hostname);
		xfree(node->os);
		xfree(node->partitions);
		xfree(node->reason);
		select_g_select_nodeinfo_free(node->select_nodeinfo);
		node->select_nodeinfo = NULL;
		xfree(node->tres_fmt_str);
		xfree(node->version);
		/* Do NOT free node, it is an element of an array */
	}
}


/*
 * slurm_free_partition_info_msg - free the partition information
 *	response message
 * IN msg - pointer to partition information response message
 * NOTE: buffer is loaded by slurm_load_partitions
 */
extern void slurm_free_partition_info_msg(partition_info_msg_t * msg)
{
	if (msg) {
		if (msg->partition_array) {
			_free_all_partitions(msg);
			xfree(msg->partition_array);
		}
		xfree(msg);
	}
}

static void  _free_all_partitions(partition_info_msg_t *msg)
{
	int i;

	if ((msg == NULL) ||
	    (msg->partition_array == NULL))
		return;

	for (i = 0; i < msg->record_count; i++)
		slurm_free_partition_info_members(
			&msg->partition_array[i]);

}

extern void slurm_free_partition_info_members(partition_info_t * part)
{
	if (part) {
		xfree(part->allow_alloc_nodes);
		xfree(part->allow_accounts);
		xfree(part->allow_groups);
		xfree(part->allow_qos);
		xfree(part->alternate);
		xfree(part->billing_weights_str);
		xfree(part->cluster_name);
		xfree(part->deny_accounts);
		xfree(part->deny_qos);
		FREE_NULL_LIST(part->job_defaults_list);
		xfree(part->job_defaults_str);
		xfree(part->name);
		xfree(part->nodes);
		xfree(part->node_inx);
		xfree(part->qos_char);
		xfree(part->tres_fmt_str);
	}
}

/*
 * slurm_free_reserve_info_msg - free the reservation information
 *	response message
 * IN msg - pointer to reservation information response message
 * NOTE: buffer is loaded by slurm_load_reservation
 */
extern void slurm_free_reservation_info_msg(reserve_info_msg_t * msg)
{
	if (msg) {
		if (msg->reservation_array) {
			_free_all_reservations(msg);
			xfree(msg->reservation_array);
		}
		xfree(msg);
	}
}

static void  _free_all_reservations(reserve_info_msg_t *msg)
{
	int i;

	if ((msg == NULL) ||
	    (msg->reservation_array == NULL))
		return;

	for (i = 0; i < msg->record_count; i++) {
		slurm_free_reserve_info_members(
			&msg->reservation_array[i]);
	}

}

extern void slurm_free_reserve_info_members(reserve_info_t * resv)
{
	int i;
	if (resv) {
		xfree(resv->accounts);
		xfree(resv->burst_buffer);
		if (resv->core_spec) {
			for (i = 0; i < resv->core_spec_cnt; i++) {
				xfree(resv->core_spec[i].node_name);
				xfree(resv->core_spec[i].core_id);
			}
			xfree(resv->core_spec);
		}
		xfree(resv->features);
		xfree(resv->licenses);
		xfree(resv->name);
		xfree(resv->node_inx);
		xfree(resv->node_list);
		xfree(resv->partition);
		xfree(resv->tres_str);
		xfree(resv->users);
	}
}

/*
 * slurm_free_topo_info_msg - free the switch topology configuration
 *	information response message
 * IN msg - pointer to switch topology configuration response message
 * NOTE: buffer is loaded by slurm_load_topo.
 */
extern void slurm_free_topo_info_msg(topo_info_response_msg_t *msg)
{
	int i;

	if (msg) {
		if (msg->topo_array) {
			for (i = 0; i < msg->record_count; i++) {
				xfree(msg->topo_array[i].name);
				xfree(msg->topo_array[i].nodes);
				xfree(msg->topo_array[i].switches);
			}
			xfree(msg->topo_array);
		}
		xfree(msg);
	}
}

/*
 * slurm_free_burst_buffer_info_msg - free buffer returned by
 *	slurm_load_burst_buffer
 * IN burst_buffer_info_msg_ptr - pointer to burst_buffer_info_msg_t
 * RET 0 or a slurm error code
 */
extern void slurm_free_burst_buffer_info_msg(burst_buffer_info_msg_t *msg)
{
	int i, j;
	burst_buffer_info_t *bb_info_ptr;
	burst_buffer_resv_t *bb_resv_ptr;

	if (msg) {
		for (i = 0, bb_info_ptr = msg->burst_buffer_array;
		     i < msg->record_count; i++, bb_info_ptr++) {
			xfree(bb_info_ptr->allow_users);
			xfree(bb_info_ptr->create_buffer);
			xfree(bb_info_ptr->deny_users);
			xfree(bb_info_ptr->destroy_buffer);
			xfree(bb_info_ptr->get_sys_state);
			xfree(bb_info_ptr->get_sys_status);
			xfree(bb_info_ptr->name);
			xfree(bb_info_ptr->start_stage_in);
			xfree(bb_info_ptr->start_stage_out);
			xfree(bb_info_ptr->stop_stage_in);
			xfree(bb_info_ptr->stop_stage_out);
			for (j = 0,
			     bb_resv_ptr = bb_info_ptr->burst_buffer_resv_ptr;
			     j < bb_info_ptr->buffer_count;
			     j++, bb_resv_ptr++) {
				xfree(bb_resv_ptr->account);
				xfree(bb_resv_ptr->name);
				xfree(bb_resv_ptr->partition);
				xfree(bb_resv_ptr->pool);
				xfree(bb_resv_ptr->qos);
			}
			xfree(bb_info_ptr->burst_buffer_resv_ptr);
			xfree(bb_info_ptr->burst_buffer_use_ptr);
		}
		xfree(msg->burst_buffer_array);
		xfree(msg);
	}
}

extern void slurm_free_file_bcast_msg(file_bcast_msg_t *msg)
{
	if (msg) {
		xfree(msg->block);
		xfree(msg->fname);
		xfree(msg->user_name);
		delete_sbcast_cred(msg->cred);
		xfree(msg);
	}
}

extern void slurm_free_step_complete_msg(step_complete_msg_t *msg)
{
	if (msg) {
		jobacctinfo_destroy(msg->jobacct);
		xfree(msg);
	}
}

extern void slurm_free_job_step_stat(void *object)
{
	job_step_stat_t *msg = (job_step_stat_t *)object;
	if (msg) {
		jobacctinfo_destroy(msg->jobacct);
		slurm_free_job_step_pids(msg->step_pids);
		xfree(msg);
	}
}

extern void slurm_free_job_step_pids(void *object)
{
	job_step_pids_t *msg = (job_step_pids_t *)object;
	if (msg) {
		xfree(msg->node_name);
		xfree(msg->pid);
		xfree(msg);
	}
}

extern void slurm_free_network_callerid_msg(network_callerid_msg_t *mesg)
{
	xfree(mesg);
}

extern void slurm_free_network_callerid_resp(network_callerid_resp_t *resp)
{
	if (resp) {
		xfree(resp->node_name);
		xfree(resp);
	}
}

extern void slurm_free_trigger_msg(trigger_info_msg_t *msg)
{
	int i;

	if (msg->trigger_array) {
		for (i = 0; i < msg->record_count; i++) {
			xfree(msg->trigger_array[i].res_id);
			xfree(msg->trigger_array[i].program);
		}
		xfree(msg->trigger_array);
	}
	xfree(msg);
}

extern void slurm_free_set_debug_flags_msg(set_debug_flags_msg_t *msg)
{
	xfree(msg);
}

extern void slurm_free_set_debug_level_msg(set_debug_level_msg_t *msg)
{
	xfree(msg);
}

extern void slurm_destroy_assoc_shares_object(void *object)
{
	assoc_shares_object_t *obj_ptr =
		(assoc_shares_object_t *)object;

	if (obj_ptr) {
		xfree(obj_ptr->cluster);
		xfree(obj_ptr->name);
		xfree(obj_ptr->parent);
		xfree(obj_ptr->partition);
		xfree(obj_ptr->tres_run_secs);
		xfree(obj_ptr->tres_grp_mins);
		xfree(obj_ptr->usage_tres_raw);
		xfree(obj_ptr);
	}
}

extern void slurm_free_shares_request_msg(shares_request_msg_t *msg)
{
	if (msg) {
		FREE_NULL_LIST(msg->acct_list);
		FREE_NULL_LIST(msg->user_list);
		xfree(msg);
	}
}

extern void slurm_free_shares_response_msg(shares_response_msg_t *msg)
{
	if (msg) {
		int i;
		if (msg->tres_names) {
			for (i=0; i<msg->tres_cnt; i++)
				xfree(msg->tres_names[i]);
			xfree(msg->tres_names);
		}
		FREE_NULL_LIST(msg->assoc_shares_list);
		xfree(msg);
	}
}


inline void slurm_free_stats_info_request_msg(stats_info_request_msg_t *msg)
{
	xfree(msg);
}


extern void slurm_destroy_priority_factors_object(void *object)
{
	priority_factors_object_t *obj_ptr =
		(priority_factors_object_t *)object;
	xfree(obj_ptr->tres_weights);
	xfree(obj_ptr->tres_names);
	xfree(obj_ptr->priority_tres);
	xfree(obj_ptr);
}

extern void slurm_copy_priority_factors_object(priority_factors_object_t *dest,
					       priority_factors_object_t *src)
{
	int size;

	if (!dest || !src)
		return;

	size = sizeof(double) * src->tres_cnt;

	memcpy(dest, src, sizeof(priority_factors_object_t));
	dest->partition = xstrdup(src->partition);

	if (src->priority_tres) {
		dest->priority_tres = xmalloc(size);
		memcpy(dest->priority_tres, src->priority_tres, size);
	}

	if (src->tres_names) {
		int char_size = sizeof(char *) * src->tres_cnt;
		dest->tres_names = xmalloc(char_size);
		memcpy(dest->tres_names, src->tres_names, char_size);
	}

	if (src->tres_weights) {
		dest->tres_weights = xmalloc(size);
		memcpy(dest->tres_weights, src->tres_weights, size);
	}
}

extern void slurm_free_priority_factors_request_msg(
	priority_factors_request_msg_t *msg)
{
	if (msg) {
		FREE_NULL_LIST(msg->job_id_list);
		xfree(msg->partitions);
		FREE_NULL_LIST(msg->uid_list);
		xfree(msg);
	}
}

extern void slurm_free_priority_factors_response_msg(
	priority_factors_response_msg_t *msg)
{
	if (msg) {
		FREE_NULL_LIST(msg->priority_factors_list);
		xfree(msg);
	}
}


extern void slurm_free_accounting_update_msg(accounting_update_msg_t *msg)
{
	if (msg) {
		FREE_NULL_LIST(msg->update_list);
		xfree(msg);
	}
}

extern void slurm_free_comp_msg_list(void *x)
{
	slurm_msg_t *msg = (slurm_msg_t*)x;
	if (msg) {
		if (msg->data_size)
			free_buf(msg->data);
		else
			slurm_free_msg_data(msg->msg_type, msg->data);

		/* make sure the data is NULL here or we could cause a
		 * double free in slurm_free_msg
		 */
		msg->data = NULL;

		slurm_free_msg(msg);
	}
}

extern void slurm_free_composite_msg(composite_msg_t *msg)
{
	if (msg) {
		FREE_NULL_LIST(msg->msg_list);
		xfree(msg);
	}
}

extern void slurm_free_set_fs_dampening_factor_msg(
	set_fs_dampening_factor_msg_t *msg)
{
	xfree(msg);
}

extern void slurm_free_control_status_msg(control_status_msg_t *msg)
{
	xfree(msg);
}

extern void slurm_free_bb_status_req_msg(bb_status_req_msg_t *msg)
{
	int i;

	if (msg) {
		if (msg->argv) {
			for (i = 0; i < msg->argc; i++)
				xfree(msg->argv[i]);
			xfree(msg->argv);
		}
		xfree(msg);
	}
}

extern void slurm_free_bb_status_resp_msg(bb_status_resp_msg_t *msg)
{
	if (msg) {
		xfree(msg->status_resp);
		xfree(msg);
	}
}

extern void slurm_free_crontab_request_msg(crontab_request_msg_t *msg)
{
	if (!msg)
		return;

	xfree(msg);
}

extern void slurm_free_crontab_response_msg(crontab_response_msg_t *msg)
{
	if (!msg)
		return;

	xfree(msg->crontab);
	xfree(msg->disabled_lines);
	xfree(msg);
}

extern void slurm_free_crontab_update_request_msg(
	crontab_update_request_msg_t *msg)
{
	if (!msg)
		return;

	xfree(msg->crontab);
	FREE_NULL_LIST(msg->jobs);
	xfree(msg);
}

extern void slurm_free_crontab_update_response_msg(
	crontab_update_response_msg_t *msg)
{
	if (!msg)
		return;

	xfree(msg->err_msg);
	xfree(msg->failed_lines);
	xfree(msg->jobids);
	xfree(msg);
}

extern int slurm_free_msg_data(slurm_msg_type_t type, void *data)
{
	if (!data)
		return SLURM_SUCCESS;

	/* this message was never loaded */
	if ((uint16_t)type == NO_VAL16)
		return SLURM_SUCCESS;

	switch (type) {
	case RESPONSE_LAUNCH_TASKS:
		slurm_free_launch_tasks_response_msg(data);
		break;
	case MESSAGE_TASK_EXIT:
		slurm_free_task_exit_msg(data);
		break;
	case REQUEST_BUILD_INFO:
		slurm_free_last_update_msg(data);
		break;
	case REQUEST_JOB_INFO:
		slurm_free_job_info_request_msg(data);
		break;
	case REQUEST_NODE_INFO:
		slurm_free_node_info_request_msg(data);
		break;
	case REQUEST_NODE_INFO_SINGLE:
		slurm_free_node_info_single_msg(data);
		break;
	case REQUEST_PARTITION_INFO:
		slurm_free_part_info_request_msg(data);
		break;
	case MESSAGE_EPILOG_COMPLETE:
		slurm_free_epilog_complete_msg(data);
		break;
	case REQUEST_KILL_JOB:
	case REQUEST_CANCEL_JOB_STEP:
	case SRUN_STEP_SIGNAL:
		slurm_free_job_step_kill_msg(data);
		break;
	case REQUEST_COMPLETE_JOB_ALLOCATION:
		slurm_free_complete_job_allocation_msg(data);
		break;
	case REQUEST_COMPLETE_PROLOG:
		slurm_free_complete_prolog_msg(data);
		break;
	case REQUEST_COMPLETE_BATCH_SCRIPT:
		slurm_free_complete_batch_script_msg(data);
		break;
	case REQUEST_JOB_STEP_CREATE:
		slurm_free_job_step_create_request_msg(data);
		break;
	case REQUEST_JOB_STEP_INFO:
		slurm_free_job_step_info_request_msg(data);
		break;
	case RESPONSE_JOB_STEP_PIDS:
		slurm_free_job_step_pids(data);
		break;
	case REQUEST_LAUNCH_PROLOG:
		slurm_free_prolog_launch_msg(data);
		break;
	case REQUEST_RESOURCE_ALLOCATION:
	case REQUEST_JOB_WILL_RUN:
	case REQUEST_SUBMIT_BATCH_JOB:
	case REQUEST_UPDATE_JOB:
		slurm_free_job_desc_msg(data);
		break;
	case REQUEST_SIB_JOB_LOCK:
	case REQUEST_SIB_JOB_UNLOCK:
	case REQUEST_SIB_MSG:
		slurm_free_sib_msg(data);
		break;
	case REQUEST_SEND_DEP:
		slurm_free_dep_msg(data);
		break;
	case REQUEST_UPDATE_ORIGIN_DEP:
		slurm_free_dep_update_origin_msg(data);
		break;
	case RESPONSE_JOB_WILL_RUN:
		slurm_free_will_run_response_msg(data);
		break;
	case RESPONSE_SUBMIT_BATCH_JOB:
		slurm_free_submit_response_response_msg(data);
		break;
	case RESPONSE_ACCT_GATHER_UPDATE:
		slurm_free_acct_gather_node_resp_msg(data);
		break;
	case RESPONSE_NODE_REGISTRATION:
		slurm_free_node_reg_resp_msg(data);
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
	case MESSAGE_NODE_REGISTRATION_STATUS:
		slurm_free_node_registration_status_msg(data);
		break;
	case REQUEST_JOB_ALLOCATION_INFO:
	case REQUEST_JOB_END_TIME:
	case REQUEST_HET_JOB_ALLOC_INFO:
		slurm_free_job_alloc_info_msg(data);
		break;
	case REQUEST_JOB_SBCAST_CRED:
		slurm_destroy_selected_step(data);
		break;
	case REQUEST_SHUTDOWN:
		slurm_free_shutdown_msg(data);
		break;
	case REQUEST_UPDATE_FRONT_END:
		slurm_free_update_front_end_msg(data);
		break;
	case REQUEST_CREATE_NODE:
	case REQUEST_UPDATE_NODE:
	case REQUEST_DELETE_NODE:
		slurm_free_update_node_msg(data);
		break;
	case REQUEST_CREATE_PARTITION:
	case REQUEST_UPDATE_PARTITION:
		slurm_free_update_part_msg(data);
		break;
	case REQUEST_DELETE_PARTITION:
		slurm_free_delete_part_msg(data);
		break;
	case REQUEST_CREATE_RESERVATION:
	case REQUEST_UPDATE_RESERVATION:
		slurm_free_resv_desc_msg(data);
		break;
	case REQUEST_DELETE_RESERVATION:
	case RESPONSE_CREATE_RESERVATION:
		slurm_free_resv_name_msg(data);
		break;
	case REQUEST_RESERVATION_INFO:
		slurm_free_resv_info_request_msg(data);
		break;
	case REQUEST_FRONT_END_INFO:
		slurm_free_front_end_info_request_msg(data);
		break;
	case REQUEST_SUSPEND:
	case SRUN_REQUEST_SUSPEND:
		slurm_free_suspend_msg(data);
		break;
	case REQUEST_SUSPEND_INT:
		slurm_free_suspend_int_msg(data);
		break;
	case REQUEST_TOP_JOB:
		slurm_free_top_job_msg(data);
		break;
	case REQUEST_AUTH_TOKEN:
		slurm_free_token_request_msg(data);
		break;
	case RESPONSE_AUTH_TOKEN:
		slurm_free_token_response_msg(data);
		break;
	case REQUEST_JOB_REQUEUE:
		slurm_free_requeue_msg(data);
		break;
	case REQUEST_BATCH_SCRIPT:
	case REQUEST_JOB_READY:
	case REQUEST_JOB_INFO_SINGLE:
		slurm_free_job_id_msg(data);
		break;
	case RESPONSE_BATCH_SCRIPT:
		slurm_free_batch_script_msg(data);
		break;
	case REQUEST_JOB_USER_INFO:
		slurm_free_job_user_id_msg(data);
		break;
	case REQUEST_SHARE_INFO:
		slurm_free_shares_request_msg(data);
		break;
	case RESPONSE_SHARE_INFO:
		slurm_free_shares_response_msg(data);
		break;
	case REQUEST_PRIORITY_FACTORS:
		slurm_free_priority_factors_request_msg(data);
		break;
	case RESPONSE_PRIORITY_FACTORS:
		slurm_free_priority_factors_response_msg(data);
		break;
	case REQUEST_STEP_COMPLETE:
		slurm_free_step_complete_msg(data);
		break;
	case REQUEST_JOB_STEP_STAT:
	case REQUEST_JOB_STEP_PIDS:
	case REQUEST_STEP_LAYOUT:
		slurm_free_step_id(data);
		break;
	case RESPONSE_JOB_STEP_STAT:
		slurm_free_job_step_stat(data);
		break;
	case REQUEST_BATCH_JOB_LAUNCH:
		slurm_free_job_launch_msg(data);
		break;
	case REQUEST_LAUNCH_TASKS:
		slurm_free_launch_tasks_request_msg(data);
		break;
	case REQUEST_SIGNAL_TASKS:
	case REQUEST_TERMINATE_TASKS:
		slurm_free_signal_tasks_msg(data);
		break;
	case REQUEST_KILL_PREEMPTED:
	case REQUEST_KILL_TIMELIMIT:
		slurm_free_timelimit_msg(data);
		break;
	case REQUEST_REATTACH_TASKS:
		slurm_free_reattach_tasks_request_msg(data);
		break;
	case RESPONSE_REATTACH_TASKS:
		slurm_free_reattach_tasks_response_msg(data);
		break;
	case REQUEST_ABORT_JOB:
	case REQUEST_TERMINATE_JOB:
		slurm_free_kill_job_msg(data);
		break;
	case REQUEST_JOB_ID:
		slurm_free_job_id_request_msg(data);
		break;
	case REQUEST_CONFIG:
		slurm_free_config_request_msg(data);
		break;
	case REQUEST_RECONFIGURE_WITH_CONFIG:
	case RESPONSE_CONFIG:
		slurm_free_config_response_msg(data);
		break;
	case REQUEST_FILE_BCAST:
		slurm_free_file_bcast_msg(data);
		break;
	case RESPONSE_SLURM_RC:
		slurm_free_return_code_msg(data);
		break;
	case REQUEST_SET_DEBUG_FLAGS:
		slurm_free_set_debug_flags_msg(data);
		break;
	case REQUEST_SET_DEBUG_LEVEL:
	case REQUEST_SET_SCHEDLOG_LEVEL:
		slurm_free_set_debug_level_msg(data);
		break;
	case REQUEST_PING:
	case REQUEST_RECONFIGURE:
	case REQUEST_CONTROL:
	case REQUEST_CONTROL_STATUS:
	case REQUEST_TAKEOVER:
	case RESPONSE_FORWARD_FAILED:
	case REQUEST_DAEMON_STATUS:
	case REQUEST_HEALTH_CHECK:
	case REQUEST_ACCT_GATHER_UPDATE:
	case ACCOUNTING_FIRST_REG:
	case ACCOUNTING_TRES_CHANGE_DB:
	case ACCOUNTING_NODES_CHANGE_DB:
	case REQUEST_TOPO_INFO:
	case REQUEST_BURST_BUFFER_INFO:
	case ACCOUNTING_REGISTER_CTLD:
	case REQUEST_FED_INFO:
		/* No body to free */
		break;
	case RESPONSE_FED_INFO:
		slurmdb_destroy_federation_rec(data);
		break;
	case REQUEST_PERSIST_INIT:
		slurm_persist_free_init_req_msg(data);
		break;
	case PERSIST_RC:
		slurm_persist_free_rc_msg(data);
		break;
	case REQUEST_REBOOT_NODES:
		slurm_free_reboot_msg(data);
		break;
	case ACCOUNTING_UPDATE_MSG:
		slurm_free_accounting_update_msg(data);
		break;
	case RESPONSE_TOPO_INFO:
		slurm_free_topo_info_msg(data);
		break;
	case REQUEST_UPDATE_JOB_STEP:
		slurm_free_update_step_msg(data);
		break;
	case RESPONSE_PING_SLURMD:
		slurm_free_ping_slurmd_resp(data);
		break;
	case RESPONSE_JOB_ARRAY_ERRORS:
		slurm_free_job_array_resp(data);
		break;
	case RESPONSE_BURST_BUFFER_INFO:
		slurm_free_burst_buffer_info_msg(data);
		break;
	case REQUEST_TRIGGER_GET:
	case RESPONSE_TRIGGER_GET:
	case REQUEST_TRIGGER_SET:
	case REQUEST_TRIGGER_CLEAR:
	case REQUEST_TRIGGER_PULL:
		slurm_free_trigger_msg(data);
		break;
	case REQUEST_JOB_NOTIFY:
		slurm_free_job_notify_msg(data);
		break;
	case REQUEST_STATS_INFO:
		slurm_free_stats_info_request_msg(data);
		break;
	case REQUEST_LICENSE_INFO:
		slurm_free_license_info_request_msg(data);
		break;
	case REQUEST_ACCT_GATHER_ENERGY:
		slurm_free_acct_gather_energy_req_msg(data);
		break;
	case REQUEST_FORWARD_DATA:
		slurm_free_forward_data_msg(data);
		break;
	case REQUEST_NETWORK_CALLERID:
		slurm_free_network_callerid_msg(data);
		break;
	case SRUN_JOB_COMPLETE:
		slurm_free_srun_job_complete_msg(data);
		break;
	case SRUN_PING:
		slurm_free_srun_ping_msg(data);
		break;
	case SRUN_TIMEOUT:
		slurm_free_srun_timeout_msg(data);
		break;
	case SRUN_USER_MSG:
		slurm_free_srun_user_msg(data);
		break;
	case SRUN_NODE_FAIL:
		slurm_free_srun_node_fail_msg(data);
		break;
	case SRUN_STEP_MISSING:
		slurm_free_srun_step_missing_msg(data);
		break;
	case SRUN_NET_FORWARD:
		slurm_free_net_forward_msg(data);
		break;
	case PMI_KVS_GET_REQ:
		slurm_free_get_kvs_msg(data);
		break;
	case PMI_KVS_GET_RESP:
	case PMI_KVS_PUT_REQ:
		slurm_free_kvs_comm_set(data);
		break;
	case RESPONSE_RESOURCE_ALLOCATION:
		slurm_free_resource_allocation_response_msg(data);
		break;
	case REQUEST_ASSOC_MGR_INFO:
		slurm_free_assoc_mgr_info_request_msg(data);
		break;
	case REQUEST_CTLD_MULT_MSG:
	case RESPONSE_CTLD_MULT_MSG:
		slurm_free_ctld_multi_msg(data);
		break;
	case RESPONSE_JOB_INFO:
		slurm_free_job_info(data);
		break;
	case REQUEST_HET_JOB_ALLOCATION:
	case REQUEST_SUBMIT_BATCH_HET_JOB:
	case RESPONSE_HET_JOB_ALLOCATION:
		FREE_NULL_LIST(data);
		break;
	case REQUEST_SET_FS_DAMPENING_FACTOR:
		slurm_free_set_fs_dampening_factor_msg(data);
		break;
	case RESPONSE_CONTROL_STATUS:
		slurm_free_control_status_msg(data);
		break;
	case REQUEST_BURST_BUFFER_STATUS:
		slurm_free_bb_status_req_msg(data);
		break;
	case RESPONSE_BURST_BUFFER_STATUS:
		slurm_free_bb_status_resp_msg(data);
		break;
	case REQUEST_CRONTAB:
		slurm_free_crontab_request_msg(data);
		break;
	case RESPONSE_CRONTAB:
		slurm_free_crontab_response_msg(data);
		break;
	case REQUEST_UPDATE_CRONTAB:
		slurm_free_crontab_update_request_msg(data);
		break;
	case RESPONSE_UPDATE_CRONTAB:
		slurm_free_crontab_update_response_msg(data);
		break;
	default:
		error("invalid type trying to be freed %u", type);
		break;
	}
	return SLURM_SUCCESS;
}

extern uint32_t slurm_get_return_code(slurm_msg_type_t type, void *data)
{
	uint32_t rc = 0;

	switch (type) {
	case MESSAGE_EPILOG_COMPLETE:
		rc = ((epilog_complete_msg_t *)data)->return_code;
		break;
	case RESPONSE_JOB_STEP_STAT:
		rc = ((job_step_stat_t *)data)->return_code;
		break;
	case RESPONSE_REATTACH_TASKS:
		rc = ((reattach_tasks_response_msg_t *)data)->return_code;
		break;
	case RESPONSE_JOB_ID:
		rc = ((job_id_response_msg_t *)data)->return_code;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *)data)->return_code;
		break;
	case RESPONSE_PING_SLURMD:
		rc = SLURM_SUCCESS;
		break;
	case RESPONSE_ACCT_GATHER_UPDATE:
		rc = SLURM_SUCCESS;
		break;
	case RESPONSE_FORWARD_FAILED:
		/* There may be other reasons for the failure, but
		 * this may be a slurm_msg_t data type lacking the
		 * err field found in ret_data_info_t data type */
		rc = SLURM_COMMUNICATIONS_CONNECTION_ERROR;
		break;
	default:
		error("don't know the rc for type %u returning %u", type, rc);
		break;
	}
	return rc;
}

extern void slurm_free_job_notify_msg(job_notify_msg_t * msg)
{
	if (msg) {
		xfree(msg->message);
		xfree(msg);
	}
}

extern void slurm_free_ctld_multi_msg(ctld_list_msg_t *msg)
{
	if (msg) {
		FREE_NULL_LIST(msg->my_list);
		xfree(msg);
	}
}

/*
 *  Sanitize spank_job_env by prepending "SPANK_" to all entries,
 *   thus rendering them harmless in environment of scripts and
 *   programs running with root privileges.
 */
extern bool valid_spank_job_env(char **spank_job_env,
				uint32_t spank_job_env_size, uid_t uid)
{
	int i;
	char *entry;

	for (i=0; i<spank_job_env_size; i++) {
		if (!xstrncmp(spank_job_env[i], "SPANK_", 6))
			continue;
		entry = spank_job_env[i];
		spank_job_env[i] = xstrdup_printf ("SPANK_%s", entry);
		xfree (entry);
	}
	return true;
}

/* slurm_free_license_info()
 *
 * Free the license info returned previously
 * from the controller.
 */
extern void
slurm_free_license_info_msg(license_info_msg_t *msg)
{
	int cc;

	if (msg == NULL)
		return;

	if (msg->lic_array) {
		for (cc = 0; cc < msg->num_lic; cc++) {
			xfree(msg->lic_array[cc].name);
		}
		xfree(msg->lic_array);
	}
	xfree(msg);
}
extern void slurm_free_license_info_request_msg(license_info_request_msg_t *msg)
{
	xfree(msg);
}

/*
 * rpc_num2string()
 *
 * Given a protocol opcode return its string
 * description mapping the slurm_msg_type_t
 * to its name.
 */
char *
rpc_num2string(uint16_t opcode)
{
	static char buf[16];

	switch (opcode) {
	case REQUEST_NODE_REGISTRATION_STATUS:			/* 1001 */
		return "REQUEST_NODE_REGISTRATION_STATUS";
	case MESSAGE_NODE_REGISTRATION_STATUS:
		return "MESSAGE_NODE_REGISTRATION_STATUS";
	case REQUEST_RECONFIGURE:
		return "REQUEST_RECONFIGURE";
	case REQUEST_RECONFIGURE_WITH_CONFIG:
		return "REQUEST_RECONFIGURE_WITH_CONFIG";
	case REQUEST_SHUTDOWN:					/* 1005 */
		return "REQUEST_SHUTDOWN";

	case REQUEST_PING:					/* 1008 */
		return "REQUEST_PING";
	case REQUEST_CONTROL:
		return "REQUEST_CONTROL";
	case REQUEST_SET_DEBUG_LEVEL:
		return "REQUEST_SET_DEBUG_LEVEL";		/* 1010 */
	case REQUEST_HEALTH_CHECK:
		return "REQUEST_HEALTH_CHECK";
	case REQUEST_TAKEOVER:
		return "REQUEST_TAKEOVER";
	case REQUEST_SET_SCHEDLOG_LEVEL:
		return "REQUEST_SET_SCHEDLOG_LEVEL";
	case REQUEST_SET_DEBUG_FLAGS:
		return "REQUEST_SET_DEBUG_FLAGS";
	case REQUEST_REBOOT_NODES:
		return "REQUEST_REBOOT_NODES";
	case RESPONSE_PING_SLURMD:
		return "RESPONSE_PING_SLURMD";
	case REQUEST_ACCT_GATHER_UPDATE:
		return "REQUEST_ACCT_GATHER_UPDATE";
	case RESPONSE_ACCT_GATHER_UPDATE:
		return "RESPONSE_ACCT_GATHER_UPDATE";
	case REQUEST_ACCT_GATHER_ENERGY:
		return "REQUEST_ACCT_GATHER_ENERGY";		/* 1020 */
	case RESPONSE_ACCT_GATHER_ENERGY:
		return "RESPONSE_ACCT_GATHER_ENERGY";
	case REQUEST_LICENSE_INFO:
		return "REQUEST_LICENSE_INFO";
	case RESPONSE_LICENSE_INFO:
		return "RESPONSE_LICENSE_INFO";
	case REQUEST_SET_FS_DAMPENING_FACTOR:
		return "REQUEST_SET_FS_DAMPENING_FACTOR,";

	case REQUEST_BUILD_INFO:				/* 2001 */
		return "REQUEST_BUILD_INFO";
	case RESPONSE_BUILD_INFO:
		return "RESPONSE_BUILD_INFO";
	case REQUEST_JOB_INFO:
		return "REQUEST_JOB_INFO";
	case RESPONSE_JOB_INFO:
		return "RESPONSE_JOB_INFO";
	case REQUEST_JOB_STEP_INFO:
		return "REQUEST_JOB_STEP_INFO";
	case RESPONSE_JOB_STEP_INFO:
		return "RESPONSE_JOB_STEP_INFO";
	case REQUEST_NODE_INFO:
		return "REQUEST_NODE_INFO";
	case RESPONSE_NODE_INFO:
		return "RESPONSE_NODE_INFO";
	case REQUEST_PARTITION_INFO:
		return "REQUEST_PARTITION_INFO";
	case RESPONSE_PARTITION_INFO:
		return "RESPONSE_PARTITION_INFO";		/* 2010 */

	case REQUEST_JOB_ID:					/* 2013 */
		return "REQUEST_JOB_ID";
	case RESPONSE_JOB_ID:
		return "RESPONSE_JOB_ID";
	case REQUEST_CONFIG:
		return "REQUEST_CONFIG";
	case RESPONSE_CONFIG:
		return "RESPONSE_CONFIG";
	case REQUEST_TRIGGER_SET:				/* 2017 */
		return "REQUEST_TRIGGER_SET";
	case REQUEST_TRIGGER_GET:
		return "REQUEST_TRIGGER_GET";
	case REQUEST_TRIGGER_CLEAR:
		return "REQUEST_TRIGGER_CLEAR";
	case RESPONSE_TRIGGER_GET:
		return "RESPONSE_TRIGGER_GET";			/* 2020 */
	case REQUEST_JOB_INFO_SINGLE:
		return "REQUEST_JOB_INFO_SINGLE";
	case REQUEST_SHARE_INFO:
		return "REQUEST_SHARE_INFO";
	case RESPONSE_SHARE_INFO:
		return "RESPONSE_SHARE_INFO";
	case REQUEST_RESERVATION_INFO:
		return "REQUEST_RESERVATION_INFO";
	case RESPONSE_RESERVATION_INFO:
		return "RESPONSE_RESERVATION_INFO";
	case REQUEST_PRIORITY_FACTORS:
		return "REQUEST_PRIORITY_FACTORS";
	case RESPONSE_PRIORITY_FACTORS:
		return "RESPONSE_PRIORITY_FACTORS";
	case REQUEST_TOPO_INFO:
		return "REQUEST_TOPO_INFO";
	case RESPONSE_TOPO_INFO:
		return "RESPONSE_TOPO_INFO";
	case REQUEST_TRIGGER_PULL:
		return "REQUEST_TRIGGER_PULL";			/* 2030 */
	case REQUEST_FRONT_END_INFO:
		return "REQUEST_FRONT_END_INFO";
	case RESPONSE_FRONT_END_INFO:
		return "RESPONSE_FRONT_END_INFO";

	case REQUEST_STATS_INFO:				/* 2035 */
		return "REQUEST_STATS_INFO";
	case RESPONSE_STATS_INFO:
		return "RESPONSE_STATS_INFO";
	case REQUEST_BURST_BUFFER_INFO:
		return "REQUEST_BURST_BUFFER_INFO";
	case RESPONSE_BURST_BUFFER_INFO:
		return "RESPONSE_BURST_BUFFER_INFO";
	case REQUEST_JOB_USER_INFO:
		return "REQUEST_JOB_USER_INFO";
	case REQUEST_NODE_INFO_SINGLE:				/* 2040 */
		return "REQUEST_NODE_INFO_SINGLE";

	case REQUEST_ASSOC_MGR_INFO:				/* 2043 */
		return "REQUEST_ASSOC_MGR_INFO";
	case RESPONSE_ASSOC_MGR_INFO:
		return "RESPONSE_ASSOC_MGR_INFO";

	case REQUEST_FED_INFO:					/* 2048 */
		return "REQUEST_FED_INFO";
	case RESPONSE_FED_INFO:
		return "RESPONSE_FED_INFO";
	case REQUEST_BATCH_SCRIPT:
		return "REQUEST_BATCH_SCRIPT";
	case RESPONSE_BATCH_SCRIPT:
		return "RESPONSE_BATCH_SCRIPT";
	case REQUEST_CONTROL_STATUS:
		return "REQUEST_CONTROL_STATUS";
	case RESPONSE_CONTROL_STATUS:
		return "RESPONSE_CONTROL_STATUS";
	case REQUEST_BURST_BUFFER_STATUS:
		return "REQUEST_BURST_BUFFER_STATUS";
	case RESPONSE_BURST_BUFFER_STATUS:
		return "RESPONSE_BURST_BUFFER_STATUS";

	case REQUEST_CRONTAB:					/* 2200 */
		return "REQUEST_CRONTAB";
	case RESPONSE_CRONTAB:
		return "RESPONSE_CRONTAB";
	case REQUEST_UPDATE_CRONTAB:
		return "REQUEST_UPDATE_CRONTAB";
	case RESPONSE_UPDATE_CRONTAB:
		return "RESPONSE_UPDATE_CRONTAB";

	case REQUEST_UPDATE_JOB:				/* 3001 */
		return "REQUEST_UPDATE_JOB";
	case REQUEST_UPDATE_NODE:
		return "REQUEST_UPDATE_NODE";
	case REQUEST_CREATE_PARTITION:
		return "REQUEST_CREATE_PARTITION";
	case REQUEST_DELETE_PARTITION:
		return "REQUEST_DELETE_PARTITION";
	case REQUEST_UPDATE_PARTITION:
		return "REQUEST_UPDATE_PARTITION";
	case REQUEST_CREATE_RESERVATION:
		return "REQUEST_CREATE_RESERVATION";
	case RESPONSE_CREATE_RESERVATION:
		return "RESPONSE_CREATE_RESERVATION";
	case REQUEST_DELETE_RESERVATION:
		return "REQUEST_DELETE_RESERVATION";
	case REQUEST_UPDATE_RESERVATION:
		return "REQUEST_UPDATE_RESERVATION";
	case REQUEST_UPDATE_FRONT_END:				/* 3011 */
		return "REQUEST_UPDATE_FRONT_END";
	case REQUEST_DELETE_NODE:
		return "REQUEST_DELETE_NODE";
	case REQUEST_CREATE_NODE:
		return "REQUEST_CREATE_NODE";

	case REQUEST_RESOURCE_ALLOCATION:			/* 4001 */
		return "REQUEST_RESOURCE_ALLOCATION";
	case RESPONSE_RESOURCE_ALLOCATION:
		return "RESPONSE_RESOURCE_ALLOCATION";
	case REQUEST_SUBMIT_BATCH_JOB:
		return "REQUEST_SUBMIT_BATCH_JOB";
	case RESPONSE_SUBMIT_BATCH_JOB:
		return "RESPONSE_SUBMIT_BATCH_JOB";
	case REQUEST_BATCH_JOB_LAUNCH:
		return "REQUEST_BATCH_JOB_LAUNCH";
	case REQUEST_CANCEL_JOB:
		return "REQUEST_CANCEL_JOB";

	case REQUEST_JOB_WILL_RUN:				/* 4012 */
		return "REQUEST_JOB_WILL_RUN";
	case RESPONSE_JOB_WILL_RUN:
		return "RESPONSE_JOB_WILL_RUN";
	case REQUEST_JOB_ALLOCATION_INFO:
		return "REQUEST_JOB_ALLOCATION_INFO";
	case RESPONSE_JOB_ALLOCATION_INFO:
		return "RESPONSE_JOB_ALLOCATION_INFO";
	case REQUEST_HET_JOB_ALLOCATION:
		return "REQUEST_HET_JOB_ALLOCATION";
	case RESPONSE_HET_JOB_ALLOCATION:
		return "RESPONSE_HET_JOB_ALLOCATION";

	case REQUEST_JOB_READY:
		return "REQUEST_JOB_READY";
	case RESPONSE_JOB_READY:				/* 4020 */
		return "RESPONSE_JOB_READY";
	case REQUEST_JOB_END_TIME:
		return "REQUEST_JOB_END_TIME";
	case REQUEST_JOB_NOTIFY:
		return "REQUEST_JOB_NOTIFY";
	case REQUEST_JOB_SBCAST_CRED:
		return "REQUEST_JOB_SBCAST_CRED";
	case RESPONSE_JOB_SBCAST_CRED:
		return "RESPONSE_JOB_SBCAST_CRED";

	case REQUEST_SIB_JOB_LOCK:				/* 4050 */
		return "REQUEST_SIB_JOB_LOCK";
	case REQUEST_SIB_JOB_UNLOCK:
		return "REQUEST_SIB_JOB_UNLOCK";
	case REQUEST_SEND_DEP:
		return "REQUEST_SEND_DEP";
	case REQUEST_UPDATE_ORIGIN_DEP:
		return "REQUEST_UPDATE_ORIGIN_DEP";
	case REQUEST_CTLD_MULT_MSG:
		return "REQUEST_CTLD_MULT_MSG";
	case RESPONSE_CTLD_MULT_MSG:
		return "RESPONSE_CTLD_MULT_MSG";
	case REQUEST_SIB_MSG:
		return "REQUEST_SIB_MSG";
	case REQUEST_HET_JOB_ALLOC_INFO:
		return "REQUEST_HET_JOB_ALLOC_INFO";
	case REQUEST_SUBMIT_BATCH_HET_JOB:
		return "REQUEST_SUBMIT_BATCH_HET_JOB";

	case REQUEST_JOB_STEP_CREATE:				/* 5001 */
		return "REQUEST_JOB_STEP_CREATE";
	case RESPONSE_JOB_STEP_CREATE:
		return "RESPONSE_JOB_STEP_CREATE";

	case REQUEST_CANCEL_JOB_STEP:				/* 5005 */
		return "REQUEST_CANCEL_JOB_STEP";

	case REQUEST_UPDATE_JOB_STEP:				/* 5007 */
		return "REQUEST_UPDATE_JOB_STEP";

	case REQUEST_SUSPEND:					/* 5014 */
		return "REQUEST_SUSPEND";
	case REQUEST_STEP_COMPLETE:
		return "REQUEST_STEP_COMPLETE";
	case REQUEST_COMPLETE_JOB_ALLOCATION:
		return "REQUEST_COMPLETE_JOB_ALLOCATION";
	case REQUEST_COMPLETE_BATCH_SCRIPT:
		return "REQUEST_COMPLETE_BATCH_SCRIPT";
	case REQUEST_JOB_STEP_STAT:
		return "REQUEST_JOB_STEP_STAT";
	case RESPONSE_JOB_STEP_STAT:				/* 5020 */
		return "RESPONSE_JOB_STEP_STAT";
	case REQUEST_STEP_LAYOUT:
		return "REQUEST_STEP_LAYOUT";
	case RESPONSE_STEP_LAYOUT:
		return "RESPONSE_STEP_LAYOUT";
	case REQUEST_JOB_REQUEUE:
		return "REQUEST_JOB_REQUEUE";
	case REQUEST_DAEMON_STATUS:
		return "REQUEST_DAEMON_STATUS";
	case RESPONSE_SLURMD_STATUS:
		return "RESPONSE_SLURMD_STATUS";

	case REQUEST_JOB_STEP_PIDS:				/* 5027 */
		return "REQUEST_JOB_STEP_PIDS";
	case RESPONSE_JOB_STEP_PIDS:
		return "RESPONSE_JOB_STEP_PIDS";
	case REQUEST_FORWARD_DATA:
		return "REQUEST_FORWARD_DATA";

	case REQUEST_SUSPEND_INT:				/* 5031 */
		return "REQUEST_SUSPEND_INT";
	case REQUEST_KILL_JOB:
		return "REQUEST_KILL_JOB";

	case RESPONSE_JOB_ARRAY_ERRORS:				/* 5034 */
		return "RESPONSE_JOB_ARRAY_ERRORS";
	case REQUEST_NETWORK_CALLERID:
		return "REQUEST_NETWORK_CALLERID";
	case RESPONSE_NETWORK_CALLERID:
		return "RESPONSE_NETWORK_CALLERID";

	case REQUEST_TOP_JOB:					/* 5038 */
		return "REQUEST_TOP_JOB";
	case REQUEST_AUTH_TOKEN:
		return "REQUEST_AUTH_TOKEN";
	case RESPONSE_AUTH_TOKEN:
		return "RESPONSE_AUTH_TOKEN";

	case REQUEST_LAUNCH_TASKS:				/* 6001 */
		return "REQUEST_LAUNCH_TASKS";
	case RESPONSE_LAUNCH_TASKS:
		return "RESPONSE_LAUNCH_TASKS";
	case MESSAGE_TASK_EXIT:
		return "MESSAGE_TASK_EXIT";
	case REQUEST_SIGNAL_TASKS:
		return "REQUEST_SIGNAL_TASKS";

	case REQUEST_TERMINATE_TASKS:				/* 6006 */
		return "REQUEST_TERMINATE_TASKS";
	case REQUEST_REATTACH_TASKS:
		return "REQUEST_REATTACH_TASKS";
	case RESPONSE_REATTACH_TASKS:
		return "RESPONSE_REATTACH_TASKS";
	case REQUEST_KILL_TIMELIMIT:
		return "REQUEST_KILL_TIMELIMIT";

	case REQUEST_TERMINATE_JOB:				/* 6011 */
		return "REQUEST_TERMINATE_JOB";
	case MESSAGE_EPILOG_COMPLETE:
		return "MESSAGE_EPILOG_COMPLETE";
	case REQUEST_ABORT_JOB:
		return "REQUEST_ABORT_JOB";
	case REQUEST_FILE_BCAST:
		return "REQUEST_FILE_BCAST";

	case REQUEST_KILL_PREEMPTED:				/* 6016 */
		return "REQUEST_KILL_PREEMPTED";
	case REQUEST_LAUNCH_PROLOG:
		return "REQUEST_LAUNCH_PROLOG";
	case REQUEST_COMPLETE_PROLOG:
		return "REQUEST_COMPLETE_PROLOG";
	case RESPONSE_PROLOG_EXECUTING:				/* 6019 */
		return "RESPONSE_PROLOG_EXECUTING";

	case SRUN_PING:						/* 7001 */
		return "SRUN_PING";
	case SRUN_TIMEOUT:
		return "SRUN_TIMEOUT";
	case SRUN_NODE_FAIL:
		return "SRUN_NODE_FAIL";
	case SRUN_JOB_COMPLETE:
		return "SRUN_JOB_COMPLETE";
	case SRUN_USER_MSG:
		return "SRUN_USER_MSG";

	case SRUN_STEP_MISSING:					/* 7007 */
		return "SRUN_STEP_MISSING";
	case SRUN_REQUEST_SUSPEND:
		return "SRUN_REQUEST_SUSPEND";
	case SRUN_STEP_SIGNAL:
		return "SRUN_STEP_SIGNAL";
	case SRUN_NET_FORWARD:
		return "SRUN_NET_FORWARD";

	case PMI_KVS_PUT_REQ:					/* 7201 */
		return "PMI_KVS_PUT_REQ";

	case PMI_KVS_GET_REQ:					/* 7203 */
		return "PMI_KVS_GET_REQ";
	case PMI_KVS_GET_RESP:
		return "PMI_KVS_GET_RESP";

	case RESPONSE_SLURM_RC:					/* 8001 */
		return "RESPONSE_SLURM_RC";
	case RESPONSE_SLURM_RC_MSG:
		return "RESPONSE_SLURM_RC_MSG";
	case RESPONSE_SLURM_REROUTE_MSG:
		return "RESPONSE_SLURM_REROUTE_MSG";

	case RESPONSE_FORWARD_FAILED:				/* 9001 */
		return "RESPONSE_FORWARD_FAILED";

	case ACCOUNTING_UPDATE_MSG:				/* 10001 */
		return "ACCOUNTING_UPDATE_MSG";
	case ACCOUNTING_FIRST_REG:
		return "ACCOUNTING_FIRST_REG";
	case ACCOUNTING_REGISTER_CTLD:
		return "ACCOUNTING_REGISTER_CTLD";
	case ACCOUNTING_TRES_CHANGE_DB:
		return "ACCOUNTING_TRES_CHANGE_DB";
	case ACCOUNTING_NODES_CHANGE_DB:
		return "ACCOUNTING_NODES_CHANGE_DB";

	case REQUEST_PERSIST_INIT:
		return "REQUEST_PERSIST_INIT";
	case PERSIST_RC:
		return "PERSIST_RC";

	case SLURMSCRIPTD_REQUEST_FLUSH:
		return "SLURMSCRIPTD_REQUEST_FLUSH";
	case SLURMSCRIPTD_REQUEST_FLUSH_JOB:
		return "SLURMSCRIPTD_REQUEST_FLUSH_JOB";
	case SLURMSCRIPTD_REQUEST_RECONFIG:
		return "SLURMSCRIPTD_REQUEST_RECONFIG";
	case SLURMSCRIPTD_REQUEST_RUN_SCRIPT:
		return "SLURMSCRIPTD_REQUEST_RUN_SCRIPT";
	case SLURMSCRIPTD_REQUEST_SCRIPT_COMPLETE:
		return "SLURMSCRIPTD_REQUEST_SCRIPT_COMPLETE";
	case SLURMSCRIPTD_REQUEST_UPDATE_DEBUG_FLAGS:
		return "SLURMSCRIPTD_REQUEST_UPDATE_DEBUG_FLAGS";
	case SLURMSCRIPTD_REQUEST_UPDATE_LOG:
		return "SLURMSCRIPTD_REQUEST_UPDATE_LOG";
	case SLURMSCRIPTD_SHUTDOWN:
		return "SLURMSCRIPTD_SHUTDOWN";
	default:
		(void) snprintf(buf, sizeof(buf), "%u", opcode);
		return buf;
	}
}

extern char *
slurm_bb_flags2str(uint32_t bb_flags)
{
	static char bb_str[1024];

	bb_str[0] = '\0';
	if (bb_flags & BB_FLAG_DISABLE_PERSISTENT) {
		if (bb_str[0])
			strcat(bb_str, ",");
		strcat(bb_str, "DisablePersistent");
	}
	if (bb_flags & BB_FLAG_EMULATE_CRAY) {
		if (bb_str[0])
			strcat(bb_str, ",");
		strcat(bb_str, "EmulateCray");
	}
	if (bb_flags & BB_FLAG_ENABLE_PERSISTENT) {
		if (bb_str[0])
			strcat(bb_str, ",");
		strcat(bb_str, "EnablePersistent");
	}
	if (bb_flags & BB_FLAG_PRIVATE_DATA) {
		if (bb_str[0])
			strcat(bb_str, ",");
		strcat(bb_str, "PrivateData");
	}
	if (bb_flags & BB_FLAG_TEARDOWN_FAILURE) {
		if (bb_str[0])
			strcat(bb_str, ",");
		strcat(bb_str, "TeardownFailure");
	}

	return bb_str;
}

extern uint32_t slurm_bb_str2flags(char *bb_str)
{
	uint32_t bb_flags = 0;

	if (xstrcasestr(bb_str, "DisablePersistent"))
		bb_flags |= BB_FLAG_DISABLE_PERSISTENT;
	if (xstrcasestr(bb_str, "EmulateCray"))
		bb_flags |= BB_FLAG_EMULATE_CRAY;
	if (xstrcasestr(bb_str, "EnablePersistent"))
		bb_flags |= BB_FLAG_ENABLE_PERSISTENT;
	if (xstrcasestr(bb_str, "PrivateData"))
		bb_flags |= BB_FLAG_PRIVATE_DATA;
	if (xstrcasestr(bb_str, "TeardownFailure"))
		bb_flags |= BB_FLAG_TEARDOWN_FAILURE;

	return bb_flags;
}

extern void
slurm_free_assoc_mgr_info_msg(assoc_mgr_info_msg_t *msg)
{
	if (!msg)
		return;

	FREE_NULL_LIST(msg->assoc_list);
	FREE_NULL_LIST(msg->qos_list);
	if (msg->tres_names) {
		int i;
		for (i=0; i<msg->tres_cnt; i++)
			xfree(msg->tres_names[i]);
		xfree(msg->tres_names);
	}
	FREE_NULL_LIST(msg->user_list);
	xfree(msg);
}

extern void slurm_free_assoc_mgr_info_request_members(
	assoc_mgr_info_request_msg_t *msg)
{
	if (!msg)
		return;

	FREE_NULL_LIST(msg->acct_list);
	FREE_NULL_LIST(msg->qos_list);
	FREE_NULL_LIST(msg->user_list);
}

extern void slurm_free_assoc_mgr_info_request_msg(
	assoc_mgr_info_request_msg_t *msg)
{
	if (!msg)
		return;

	slurm_free_assoc_mgr_info_request_members(msg);
	xfree(msg);
}

extern int parse_part_enforce_type(char *enforce_part_type, uint16_t *param)
{
	int rc = SLURM_SUCCESS;

	char *value = xstrdup(enforce_part_type);

	if (!xstrcasecmp(value, "yes")
		|| !xstrcasecmp(value, "up")
		|| !xstrcasecmp(value, "true")
		|| !xstrcasecmp(value, "1") || !xstrcasecmp(value, "any")) {
		*param = PARTITION_ENFORCE_ANY;
	} else if (!xstrcasecmp(value, "no")
		   || !xstrcasecmp(value, "down")
		   || !xstrcasecmp(value, "false")
		   || !xstrcasecmp(value, "0")) {
		*param = PARTITION_ENFORCE_NONE;
	} else if (!xstrcasecmp(value, "all")) {
		*param = PARTITION_ENFORCE_ALL;
	} else {
		error("Bad EnforcePartLimits: %s\n", value);
		rc = SLURM_ERROR;
	}

	xfree(value);
	return rc;
}

extern char * parse_part_enforce_type_2str (uint16_t type)
{
	static char type_str[1024];

	if (type == PARTITION_ENFORCE_NONE) {
		strcpy(type_str, "NO");
	} else if (type == PARTITION_ENFORCE_ANY) {
		strcpy(type_str, "ANY");
	} else if (type == PARTITION_ENFORCE_ALL) {
		strcpy(type_str, "ALL");
	}

	return type_str;
}

/* Return true if this cluster_name is in a federation */
extern bool cluster_in_federation(void *ptr, char *cluster_name)
{
	slurmdb_federation_rec_t *fed = (slurmdb_federation_rec_t *) ptr;
	slurmdb_cluster_rec_t *cluster;
	ListIterator iter;
	bool status = false;

	if (!fed || !fed->cluster_list)		/* NULL if no federations */
		return status;
	iter = list_iterator_create(fed->cluster_list);
	while ((cluster = (slurmdb_cluster_rec_t *) list_next(iter))) {
		if (!xstrcasecmp(cluster->name, cluster_name)) {
			status = true;
			break;
		}
	}
	list_iterator_destroy(iter);
	return status;
}

/* Find where cluster_name nodes start in the node_array */
extern int get_cluster_node_offset(char *cluster_name,
				   node_info_msg_t *node_info_ptr)
{
	int offset;

	xassert(cluster_name);
	xassert(node_info_ptr);

	for (offset = 0; offset < node_info_ptr->record_count; offset++)
		if (!xstrcmp(cluster_name,
			     node_info_ptr->node_array[offset].cluster_name))
			return offset;

	return 0;
}

extern void print_multi_line_string(char *user_msg, int inx,
				    log_level_t log_lvl)
{
	char *line, *buf, *ptrptr = NULL;

	if (!user_msg)
		return;

	buf = xstrdup(user_msg);
	line = strtok_r(buf, "\n", &ptrptr);
	while (line) {
		if (inx == -1)
			log_var(log_lvl, "%s", line);
		else
			log_var(log_lvl, "%d: %s", inx, line);
		line = strtok_r(NULL, "\n", &ptrptr);
	}
	xfree(buf);
}

/*
 * Given a numeric suffix, return the equivalent multiplier for the numeric
 * portion. For example: "k" returns 1024, "KB" returns 1000, etc.
 * The return value for an invalid suffix is NO_VAL64.
 */
extern uint64_t suffix_mult(char *suffix)
{
	uint64_t multiplier;

	if (!suffix || (suffix[0] == '\0')) {
		multiplier = 1;

	} else if (!xstrcasecmp(suffix, "k") ||
		   !xstrcasecmp(suffix, "kib")) {
		multiplier = 1024;
	} else if (!xstrcasecmp(suffix, "kb")) {
		multiplier = 1000;

	} else if (!xstrcasecmp(suffix, "m") ||
		   !xstrcasecmp(suffix, "mib")) {
		multiplier = ((uint64_t)1024 * 1024);
	} else if (!xstrcasecmp(suffix, "mb")) {
		multiplier = ((uint64_t)1000 * 1000);

	} else if (!xstrcasecmp(suffix, "g") ||
		   !xstrcasecmp(suffix, "gib")) {
		multiplier = ((uint64_t)1024 * 1024 * 1024);
	} else if (!xstrcasecmp(suffix, "gb")) {
		multiplier = ((uint64_t)1000 * 1000 * 1000);

	} else if (!xstrcasecmp(suffix, "t") ||
		   !xstrcasecmp(suffix, "tib")) {
		multiplier = ((uint64_t)1024 * 1024 * 1024 * 1024);
	} else if (!xstrcasecmp(suffix, "tb")) {
		multiplier = ((uint64_t)1000 * 1000 * 1000 * 1000);

	} else if (!xstrcasecmp(suffix, "p") ||
		   !xstrcasecmp(suffix, "pib")) {
		multiplier = ((uint64_t)1024 * 1024 * 1024 * 1024 * 1024);
	} else if (!xstrcasecmp(suffix, "pb")) {
		multiplier = ((uint64_t)1000 * 1000 * 1000 * 1000 * 1000);

	} else {
		multiplier = NO_VAL64;
	}

	return multiplier;
}

extern bool verify_step_id(slurm_step_id_t *object, slurm_step_id_t *key)
{
	if (key->job_id != object->job_id)
		return 0;

	/* Any step will do */
	if (key->step_id == NO_VAL)
		return 1;

	/*
	 * See if we have the same step id.  If we do check to see if we
	 * have the same step_het_comp or if the key's is NO_VAL,
	 * meaning we are not looking directly for a het step.
	 */
	if ((key->step_id == object->step_id) &&
	    ((key->step_het_comp == object->step_het_comp) ||
	     (key->step_het_comp == NO_VAL)))
		return 1;
	else
		return 0;
}

extern char *slurm_get_selected_step_id(
	char *job_id_str, int len,
	slurm_selected_step_t *selected_step)
{
	int pos = 0;

	pos = snprintf(job_id_str, len, "%u",
		       selected_step->step_id.job_id);
	if (pos > len)
		goto endit;

	if (selected_step->array_task_id != NO_VAL)
		pos += snprintf(job_id_str + pos, len - pos, "_%u",
				selected_step->array_task_id);
	if (pos > len)
		goto endit;

	if (selected_step->het_job_offset != NO_VAL)
		pos += snprintf(job_id_str + pos, len - pos, "+%u",
				selected_step->het_job_offset);
	if (pos > len)
		goto endit;

	if (selected_step->step_id.step_id != NO_VAL) {
		job_id_str[pos++] = '.';

		if (pos > len)
			goto endit;

		log_build_step_id_str(&selected_step->step_id,
				      job_id_str + pos, len - pos,
				      STEP_ID_FLAG_NO_PREFIX |
				      STEP_ID_FLAG_NO_JOB);
	}
endit:
	return job_id_str;
}

extern void xlate_array_task_str(char **array_task_str,
				 uint32_t array_max_tasks,
				 bitstr_t **array_bitmap)
{
	static int bitstr_len = -1;
	int buf_size, len;
	int i, i_first, i_last, i_prev, i_step = 0;
	bitstr_t *task_bitmap;
	char *out_buf = NULL;

	xassert(array_task_str);

	if (!array_task_str || !*array_task_str || !*array_task_str[0]) {
		if (array_bitmap)
			*array_bitmap = NULL;
		return;
	}

	i = strlen(*array_task_str);
	if ((i < 3) || ((*array_task_str)[1] != 'x')) {
		if (array_bitmap)
			*array_bitmap = NULL;
		return;
	}

	task_bitmap = bit_alloc(i * 4);
	if (bit_unfmt_hexmask(task_bitmap, *array_task_str) == -1)
		error("%s: bit_unfmt_hexmask error on '%s'", __func__,
		      *array_task_str);

	if (array_bitmap)
		*array_bitmap = task_bitmap;

	/* Check first for a step function */
	i_first = bit_ffs(task_bitmap);
	i_last  = bit_fls(task_bitmap);
	if (((i_last - i_first) > 10) && (bit_set_count(task_bitmap) > 5) &&
	    !bit_test(task_bitmap, i_first + 1)) {
		bool is_step = true;
		i_prev = i_first;
		for (i = i_first + 1; i <= i_last; i++) {
			if (!bit_test(task_bitmap, i))
				continue;
			if (i_step == 0) {
				i_step = i - i_prev;
			} else if ((i - i_prev) != i_step) {
				is_step = false;
				break;
			}
			i_prev = i;
		}
		if (is_step) {
			xstrfmtcat(out_buf, "%d-%d:%d",
				   i_first, i_last, i_step);
			goto out;
		}
	}

	if (bitstr_len == -1) {
		char *bitstr_len_str = getenv("SLURM_BITSTR_LEN");
		if (bitstr_len_str)
			bitstr_len = atoi(bitstr_len_str);
		if (bitstr_len < 0)
			bitstr_len = 64;
		else
			bitstr_len = MIN(bitstr_len, 4096);
	}

	if (bitstr_len > 0) {
		/* Print the first bitstr_len bytes of the bitmap string */
		buf_size = bitstr_len;
		out_buf = xmalloc(buf_size);
		bit_fmt(out_buf, buf_size, task_bitmap);
		len = strlen(out_buf);
		if (len > (buf_size - 3)) {
			for (i = 0; i < 3; i++)
				out_buf[buf_size - 2 - i] = '.';
		}
	} else {
		/* Print the full bitmap's string representation.
		 * For huge bitmaps this can take roughly one minute,
		 * so let the client do the work */
		out_buf = bit_fmt_full(task_bitmap);
	}

out:
	if (array_max_tasks)
		xstrfmtcat(out_buf, "%%%u", array_max_tasks);

	xfree(*array_task_str);
	*array_task_str = out_buf;

	if (!array_bitmap)
		bit_free(task_bitmap);
}

extern void slurm_array64_to_value_reps(uint64_t *array, uint32_t array_cnt,
					uint64_t **values,
					uint32_t **values_reps,
					uint32_t *values_cnt)
{
	uint64_t prev_value;
	int values_inx = 0;

	xassert(values);
	xassert(values_reps);
	xassert(values_cnt);

	if (!array)
		return;

	*values_cnt = 1;

	/* Figure out how big the compressed arrays should be */
	prev_value = array[0];
	for (int i = 0; i < array_cnt; i++) {
		if (prev_value != array[i]) {
			prev_value = array[i];
			(*values_cnt)++;
		}
	}

	*values = xcalloc(*values_cnt, sizeof(**values));
	*values_reps = xcalloc(*values_cnt, sizeof(**values_reps));

	prev_value = (*values)[0] = array[0];
	for (int i = 0; i < array_cnt; i++) {
		if (prev_value != array[i]) {
			prev_value = array[i];
			values_inx++;
			(*values)[values_inx] = array[i];
		}
		(*values_reps)[values_inx]++;
	}


}

extern int slurm_get_rep_count_inx(
	uint32_t *rep_count, uint32_t rep_count_size, int inx)
{
	int rep_count_sum = 0;

	for (int i = 0; i < rep_count_size; i++) {
		if (rep_count[i] == 0) {
			error("%s: rep_count should never be zero",
			      __func__);
			return -1;
		}
		rep_count_sum += rep_count[i];
		if (rep_count_sum > inx)
			return i;
	}

	return -1;
}
