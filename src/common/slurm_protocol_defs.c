/*****************************************************************************\
 *  slurm_protocol_defs.c - functions for initializing and releasing
 *	storage for RPC data structures. these are the functions used by
 *	the slurm daemons directly, not for user client use.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2015 SchedMD <http://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/forward.h"
#include "src/common/job_options.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/power.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_ext_sensors.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/switch.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/plugins/select/bluegene/bg_enums.h"

/*
** Define slurm-specific aliases for use by plugins, see slurm_xlator.h
** for details.
 */
strong_alias(preempt_mode_string, slurm_preempt_mode_string);
strong_alias(preempt_mode_num, slurm_preempt_mode_num);
strong_alias(job_reason_string, slurm_job_reason_string);
strong_alias(job_share_string, slurm_job_share_string);
strong_alias(job_state_string, slurm_job_state_string);
strong_alias(job_state_string_compact, slurm_job_state_string_compact);
strong_alias(job_state_num, slurm_job_state_num);
strong_alias(node_state_string, slurm_node_state_string);
strong_alias(node_state_string_compact, slurm_node_state_string_compact);
strong_alias(private_data_string, slurm_private_data_string);
strong_alias(accounting_enforce_string, slurm_accounting_enforce_string);
strong_alias(conn_type_string,	slurm_conn_type_string);
strong_alias(conn_type_string_full, slurm_conn_type_string_full);
strong_alias(node_use_string, slurm_node_use_string);
strong_alias(bg_block_state_string, slurm_bg_block_state_string);
strong_alias(cray_nodelist2nids, slurm_cray_nodelist2nids);
strong_alias(reservation_flags_string, slurm_reservation_flags_string);


static void _free_all_front_end_info(front_end_info_msg_t *msg);

static void _free_all_job_info (job_info_msg_t *msg);

static void _free_all_node_info (node_info_msg_t *msg);

static void _free_all_partitions (partition_info_msg_t *msg);

static void  _free_all_reservations(reserve_info_msg_t *msg);

static void _free_all_step_info (job_step_info_response_msg_t *msg);

/*
 * slurm_msg_t_init - initialize a slurm message
 * OUT msg - pointer to the slurm_msg_t structure which will be initialized
 */
extern void slurm_msg_t_init(slurm_msg_t *msg)
{
	memset(msg, 0, sizeof(slurm_msg_t));

	msg->conn_fd = -1;
	msg->msg_type = (uint16_t)NO_VAL;
	msg->protocol_version = (uint16_t)NO_VAL;

	forward_init(&msg->forward, NULL);

	return;
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
	dest->orig_addr.sin_addr.s_addr = 0;
	return;
}

extern void slurm_destroy_char(void *object)
{
	char *tmp = (char *)object;
	xfree(tmp);
}

extern void slurm_destroy_uint32_ptr(void *object)
{
	uint32_t *tmp = (uint32_t *)object;
	xfree(tmp);
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
	ret_list = list_create(slurm_destroy_char);

	while ((tmp_char = list_next(itr)))
		list_append(ret_list, xstrdup(tmp_char));

	list_iterator_destroy(itr);

	return ret_list;
}

static int _find_char_in_list(void *x, void *key)
{
	char *char1 = (char *)x;
	char *char2 = (char *)key;

	if (!xstrcasecmp(char1, char2))
		return 1;

	return 0;
}


/* returns number of objects added to list */
extern int slurm_addto_char_list(List char_list, char *names)
{
	int i = 0, start = 0;
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
		while (names[i]) {
			//info("got %d - %d = %d", i, start, i-start);
			if (quote && (names[i] == quote_c))
				break;
			else if ((names[i] == '\"') || (names[i] == '\''))
				names[i] = '`';
			else if (names[i] == '[')
			       /* Make sure there is a open bracket. This
				* check is to allow comma sperated notation
				* within the bracket ie. linux[0-1,2]  */
				first_brack = true;
			else if (names[i] == ',' && !first_brack) {
				/* Check that the string before , was
				 * not a [] notation value */
				if (!brack_not) {
					/* If there is a comma at the end just
					 * ignore it */
					if (!names[i+1])
						break;

					name = xstrndup(names+start,
							(i-start));
					//info("got %s %d", name, i-start);

					/* If we get a duplicate remove the
					 * first one and tack this on the end.
					 * This is needed for get associations
					 * with qos.
					 */
					if (list_find(itr,
						      _find_char_in_list,
						      name)) {
						list_delete_item(itr);
					} else
						count++;

					xstrtolower(name);
					list_append(char_list, name);

					list_iterator_reset(itr);

					i++;
					start = i;
					if (!names[i]) {
						info("There is a problem "
						     "with your request. "
						     "It appears you have "
						     "spaces inside your "
						     "list.");
						count = 0;
						goto endit;
					}
				} else {
					brack_not = false;
					/* Skip over the , so it is
					 * not included in the char list */
					start = ++i;
				}
			} else if (names[i] == ']') {
				brack_not = true;
				first_brack = false;
				name = xstrndup(names+start, ((i + 1)-start));
				//info("got %s %d", name, i-start);

				if ((host_list = hostlist_create(name))) {
					/* FIXME: hostlist_shift returns
					 * a malloc'd string, but we need
					 * an xmalloc'd string to add to the
					 * list. cleanup when hostlist uses
					 * xmalloc */
					while ((this_node_name =
						xstrdup((tmp_this_node_name =
						hostlist_shift (host_list))))) {
						free(tmp_this_node_name);
						/* If we get a duplicate
						 * remove the first one and tack
						 * this on the end. This is
						 * needed for get associations
						 * with qos.
						 */
						if (list_find(itr,
							    _find_char_in_list,
							    this_node_name)) {
							list_delete_item(itr);
						} else
							count++;

						xstrtolower(this_node_name);
						list_append(char_list,
							    this_node_name);

						list_iterator_reset(itr);

						start = i + 1;
					}
				}
				hostlist_destroy(host_list);
			}
			i++;
		}

		if (i-start) {
			name = xstrndup(names+start, (i-start));
			/* If we get a duplicate remove the
			 * first one and tack this on the end.
			 * This is needed for get associations
			 * with qos.
			 */
			if (list_find(itr, _find_char_in_list, name)) {
				list_delete_item(itr);
			} else
				count++;

			xstrtolower(name);
			list_append(char_list, name);
		}
	}
endit:
	list_iterator_destroy(itr);
	return count;
}

/* Parses strings such as stra,+strb,-strc and appends the default mode to each
 * string in the list if no specific mode is listed. */
extern int slurm_addto_mode_char_list(List char_list, char *names, int mode)
{
	int i=0, start=0;
	char *m_name = NULL, *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	int count = 0;
	int equal_set = 0;
	int add_set = 0;
	char *err_msg = "You can't use '=' and '+' or '-' in the same line";

	if (!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	if (!names) {
		error("You gave me an empty name list");
		return 0;
	}

	itr = list_iterator_create(char_list);
	if (names[i] == '\"' || names[i] == '\'') {
		quote_c = names[i];
		quote = 1;
		i++;
	}
	start = i;
	while(names[i]) {
		if (quote && names[i] == quote_c)
			break;
		else if (names[i] == '\"' || names[i] == '\'')
			names[i] = '`';
		else if (names[i] == ',') {
			if ((i-start) > 0) {
				int tmp_mode = mode;
				if (names[start] == '+' ||
				    names[start] == '-') {
					tmp_mode = names[start];
					start++;
				}
				name = xstrndup(names+start, (i-start));
				if (tmp_mode) {
					if (equal_set) {
						count = 0;
						error("%s", err_msg);
						goto end_it;
					}
					add_set = 1;
					m_name = xstrdup_printf(
						  "%c%s", tmp_mode, name);
				} else {
					if (add_set) {
						count = 0;
						error("%s", err_msg);
						goto end_it;
					}
					equal_set = 1;
					m_name = xstrdup_printf("%s", name);
				}
				while((tmp_char = list_next(itr))) {
					if (!strcasecmp(tmp_char, m_name))
						break;
				}
				list_iterator_reset(itr);

				if (!tmp_char) {
					list_append(char_list, m_name);
					count++;
				} else
					xfree(m_name);
				xfree(name);
			} else if (!(i-start)) {
				list_append(char_list, xstrdup(""));
				count++;
			}

			i++;
			start = i;
			if (!names[i]) {
				error("There is a problem with "
				      "your request.  It appears you "
				      "have spaces inside your list.");
				break;
			}
		}
		i++;
	}
	if ((i-start) > 0) {
		int tmp_mode = mode;
		if (names[start] == '+' ||
		    names[start] == '-') {
			tmp_mode = names[start];
			start++;
		}
		name = xstrndup(names+start, (i-start));
		if (tmp_mode) {
			if (equal_set) {
				count = 0;
				error("%s", err_msg);
				goto end_it;
			}
			m_name = xstrdup_printf(
				  "%c%s", tmp_mode, name);
		} else {
			if (add_set) {
				count = 0;
				error("%s", err_msg);
				goto end_it;
			}
			m_name = xstrdup_printf("%s", name);
		}
		while((tmp_char = list_next(itr))) {
			if (!strcasecmp(tmp_char, m_name))
				break;
		}
		list_iterator_reset(itr);

		if (!tmp_char) {
			list_append(char_list, m_name);
			count++;
		} else
			xfree(m_name);
		xfree(name);
	} else if (!(i-start)) {
		list_append(char_list, xstrdup(""));
		count++;
	}
	if (!count) {
		error("You gave me an empty name list");
	}

end_it:
	xfree(name);
	list_iterator_destroy(itr);
	return count;
}


static int _addto_step_list_internal(List step_list, char *names,
				     int start, int end)
{
	int count = 0;
	char *dot = NULL, *name;
	slurmdb_selected_step_t *selected_step = NULL;

	if ((end-start) <= 0)
		return 0;

	name = xmalloc((end-start+1));
	memcpy(name, names+start, (end-start));

	if (!isdigit(*name)) {
		fatal("Bad job/step specified: %s", name);
		xfree(name);
		return 0;
	}

	selected_step = xmalloc(sizeof(slurmdb_selected_step_t));

	if (!(dot = strstr(name, "."))) {
		debug2("No jobstep requested");
		selected_step->stepid = NO_VAL;
	} else {
		*dot++ = 0;
		/* can't use NO_VAL since that means all */
		if (!xstrcmp(dot, "batch"))
			selected_step->stepid = INFINITE;
		else if (isdigit(*dot))
			selected_step->stepid = atoi(dot);
		else
			fatal("Bad step specified: %s", name);
	}

	if (!(dot = strstr(name, "_"))) {
		debug2("No jobarray requested");
		selected_step->array_task_id = NO_VAL;
	} else {
		*dot++ = 0;
		/* INFINITE means give me all the tasks of the array */
		if (!dot)
			selected_step->array_task_id = INFINITE;
		else if (isdigit(*dot))
			selected_step->array_task_id = atoi(dot);
		else
			fatal("Bad job array element specified: %s", name);
	}

	selected_step->jobid = atoi(name);
	xfree(name);

	if (!list_find_first(step_list,
			     slurmdb_find_selected_step_in_list,
			     selected_step)) {
		list_append(step_list, selected_step);
		count++;
	} else
		slurmdb_destroy_selected_step(selected_step);

	return count;
}

/* returns number of objects added to list */
extern int slurm_addto_step_list(List step_list, char *names)
{
	int i=0, start=0;
	char quote_c = '\0';
	int quote = 0;
	int count = 0;

	if (!step_list) {
		error("No list was given to fill in");
		return 0;
	} else if (!names)
		return 0;

	if (names[i] == '\"' || names[i] == '\'') {
		quote_c = names[i];
		quote = 1;
		i++;
	}
	start = i;
	while (names[i]) {
		//info("got %d - %d = %d", i, start, i-start);
		if (quote && names[i] == quote_c)
			break;
		else if (names[i] == '\"' || names[i] == '\'')
			names[i] = '`';
		else if (names[i] == ',') {
			count += _addto_step_list_internal(
				step_list, names, start, i);
			start = i + 1;
		}
		i++;
	}

	count += _addto_step_list_internal(step_list, names, start, i);

	return count;
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

extern void slurm_free_last_update_msg(last_update_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_reboot_msg(reboot_msg_t * msg)
{
	if (msg) {
		xfree(msg->features);
		xfree(msg->node_list);
		xfree(msg);
	}
}

extern void slurm_free_shutdown_msg(shutdown_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_job_alloc_info_msg(job_alloc_info_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_step_alloc_info_msg(step_alloc_info_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_return_code_msg(return_code_msg_t * msg)
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

extern void slurm_free_job_step_id_msg(job_step_id_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_job_id_request_msg(job_id_request_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_update_step_msg(step_update_request_msg_t * msg)
{
	if (msg) {
		jobacctinfo_destroy(msg->jobacct);
		xfree(msg->name);
		xfree(msg);
	}
}

extern void slurm_free_job_id_response_msg(job_id_response_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_job_step_kill_msg(job_step_kill_msg_t * msg)
{
	xfree(msg->sjob_id);
	xfree(msg);
}

extern void slurm_free_job_info_request_msg(job_info_request_msg_t *msg)
{
	xfree(msg);
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

extern void slurm_free_job_desc_msg(job_desc_msg_t * msg)
{
	int i;

	if (msg) {
		xfree(msg->account);
		xfree(msg->acctg_freq);
		xfree(msg->alloc_node);
		if (msg->argv) {
			for (i = 0; i < msg->argc; i++)
				xfree(msg->argv[i]);
		}
		xfree(msg->argv);
		FREE_NULL_BITMAP(msg->array_bitmap);
		xfree(msg->array_inx);
		xfree(msg->blrtsimage);
		xfree(msg->burst_buffer);
		xfree(msg->ckpt_dir);
		xfree(msg->clusters);
		xfree(msg->comment);
		xfree(msg->cpu_bind);
		xfree(msg->dependency);
		for (i = 0; i < msg->env_size; i++)
			xfree(msg->environment[i]);
		xfree(msg->environment);
		xfree(msg->std_err);
		xfree(msg->exc_nodes);
		xfree(msg->features);
		xfree(msg->job_id_str);
		xfree(msg->gres);
		xfree(msg->std_in);
		xfree(msg->licenses);
		xfree(msg->linuximage);
		xfree(msg->mail_user);
		xfree(msg->mcs_label);
		xfree(msg->mem_bind);
		xfree(msg->mloaderimage);
		xfree(msg->name);
		xfree(msg->network);
		xfree(msg->qos);
		xfree(msg->std_out);
		xfree(msg->partition);
		xfree(msg->ramdiskimage);
		xfree(msg->req_nodes);
		xfree(msg->reservation);
		xfree(msg->resp_host);
		xfree(msg->script);
		select_g_select_jobinfo_free(msg->select_jobinfo);
		msg->select_jobinfo = NULL;

		for (i = 0; i < msg->spank_job_env_size; i++)
			xfree(msg->spank_job_env[i]);
		xfree(msg->spank_job_env);
		xfree(msg->tres_req_cnt);
		xfree(msg->wckey);
		xfree(msg->work_dir);
		xfree(msg);
	}
}

extern void slurm_free_prolog_launch_msg(prolog_launch_msg_t * msg)
{
	int i;

	if (msg) {
		xfree(msg->alias_list);
		xfree(msg->nodes);
		xfree(msg->partition);
		xfree(msg->std_err);
		xfree(msg->std_out);
		xfree(msg->work_dir);
		xfree(msg->user_name);

		for (i = 0; i < msg->spank_job_env_size; i++)
			xfree(msg->spank_job_env[i]);
		xfree(msg->spank_job_env);
		slurm_cred_destroy(msg->cred);

		xfree(msg);
	}
}

extern void slurm_free_complete_prolog_msg(
	complete_prolog_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_job_launch_msg(batch_job_launch_msg_t * msg)
{
	int i;

	if (msg) {
		xfree(msg->account);
		xfree(msg->acctg_freq);
		xfree(msg->user_name);
		xfree(msg->alias_list);
		xfree(msg->nodes);
		xfree(msg->partition);
		xfree(msg->cpu_bind);
		xfree(msg->cpus_per_node);
		xfree(msg->cpu_count_reps);
		xfree(msg->script);
		xfree(msg->std_err);
		xfree(msg->std_in);
		xfree(msg->std_out);
		xfree(msg->qos);
		xfree(msg->work_dir);
		xfree(msg->ckpt_dir);
		xfree(msg->restart_dir);

		if (msg->argv) {
			for (i = 0; i < msg->argc; i++)
				xfree(msg->argv[i]);
			xfree(msg->argv);
		}

		if (msg->spank_job_env) {
			for (i = 0; i < msg->spank_job_env_size; i++)
				xfree(msg->spank_job_env[i]);
			xfree(msg->spank_job_env);
		}

		if (msg->environment) {
			for (i = 0; i < msg->envc; i++)
				xfree(msg->environment[i]);
			xfree(msg->environment);
		}

		select_g_select_jobinfo_free(msg->select_jobinfo);
		msg->select_jobinfo = NULL;

		slurm_cred_destroy(msg->cred);
		xfree(msg->resv_name);
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
	if (job) {
		xfree(job->account);
		xfree(job->alloc_node);
		if (job->array_bitmap)
			bit_free((bitstr_t *) job->array_bitmap);
		xfree(job->array_task_str);
		xfree(job->batch_host);
		xfree(job->batch_script);
		xfree(job->burst_buffer);
		xfree(job->command);
		xfree(job->comment);
		xfree(job->dependency);
		xfree(job->exc_nodes);
		xfree(job->exc_node_inx);
		xfree(job->features);
		xfree(job->gres);
		xfree(job->licenses);
		xfree(job->mcs_label);
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
		xfree(job->state_desc);
		xfree(job->std_err);
		xfree(job->std_in);
		xfree(job->std_out);
		xfree(job->tres_alloc_str);
		xfree(job->tres_req_str);
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
		xfree(msg->cpu_spec_list);
		if (msg->energy)
			acct_gather_energy_destroy(msg->energy);
		xfree(msg->features_active);
		xfree(msg->features_avail);
		if (msg->gres_info)
			free_buf(msg->gres_info);
		xfree(msg->job_id);
		xfree(msg->node_name);
		xfree(msg->os);
		xfree(msg->step_id);
		if (msg->switch_nodeinfo)
			switch_g_free_node_info(&msg->switch_nodeinfo);
		xfree(msg->version);
		xfree(msg);
	}
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

extern void slurm_free_update_layout_msg(update_layout_msg_t * msg)
{
	if (msg) {
		xfree(msg->layout);
		xfree(msg->arg);
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

extern void slurm_free_resv_desc_msg(resv_desc_msg_t * msg)
{
	if (msg) {
		xfree(msg->accounts);
		xfree(msg->burst_buffer);
		xfree(msg->core_cnt);
		xfree(msg->features);
		xfree(msg->licenses);
		xfree(msg->name);
		xfree(msg->node_cnt);
		xfree(msg->node_list);
		xfree(msg->partition);
		xfree(msg->users);
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

extern void slurm_free_layout_info_request_msg(layout_info_request_msg_t * msg)
{
	if (msg) {
		xfree(msg->layout_type);
		xfree(msg->entities);
		xfree(msg);
	}
}

extern void slurm_free_layout_info_msg(layout_info_msg_t * msg)
{
	int i;

	if (msg) {
		for (i = 0; i < msg->record_count; i++)
			xfree(msg->records[i]);
		xfree(msg->records);
		xfree(msg);
	}
}

extern void slurm_free_job_step_create_request_msg(
		job_step_create_request_msg_t *msg)
{
	if (msg) {
		xfree(msg->features);
		xfree(msg->gres);
		xfree(msg->host);
		xfree(msg->name);
		xfree(msg->network);
		xfree(msg->node_list);
		xfree(msg->ckpt_dir);
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
		xfree(msg->nodes);
		select_g_select_jobinfo_free(msg->select_jobinfo);
		msg->select_jobinfo = NULL;

		for (i=0; i<msg->spank_job_env_size; i++)
			xfree(msg->spank_job_env[i]);
		xfree(msg->spank_job_env);
		xfree(msg);
	}
}

extern void slurm_free_signal_job_msg(signal_job_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_update_job_time_msg(job_time_msg_t * msg)
{
	xfree(msg);
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
	xfree(msg->cwd);
	xfree(msg->cpu_bind);
	xfree(msg->mem_bind);
	if (msg->argv) {
		for (i = 0; i < msg->argc; i++) {
			xfree(msg->argv[i]);
		}
		xfree(msg->argv);
	}
	for (i = 0; i < msg->spank_job_env_size; i++) {
		xfree(msg->spank_job_env[i]);
	}
	xfree(msg->spank_job_env);
	if (msg->nnodes && msg->global_task_ids)
		for(i=0; i<msg->nnodes; i++) {
			xfree(msg->global_task_ids[i]);
		}
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

	xfree(msg->ckpt_dir);
	xfree(msg->restart_dir);
	xfree(msg->partition);

	if (msg->switch_job)
		switch_g_free_jobinfo(msg->switch_job);

	if (msg->options)
		job_options_destroy(msg->options);

	if (msg->select_jobinfo)
		select_g_select_jobinfo_free(msg->select_jobinfo);

	xfree(msg);
}

extern void slurm_free_task_user_managed_io_stream_msg(
		task_user_managed_io_msg_t *msg)
{
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
		for (i = 0; i < msg->ntasks; i++) {
			xfree(msg->executable_names[i]);
		}
		xfree(msg->executable_names);
		xfree(msg);
	}
}

extern void slurm_free_kill_tasks_msg(kill_tasks_msg_t * msg)
{
	xfree(msg);
}

extern void slurm_free_checkpoint_tasks_msg(checkpoint_tasks_msg_t * msg)
{
	if (msg) {
		xfree(msg->image_dir);
		xfree(msg);
	}
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

extern void slurm_free_srun_exec_msg(srun_exec_msg_t *msg)
{
	int i;

	if (msg) {
		for (i = 0; i < msg->argc; i++)
			xfree(msg->argv[i]);
		xfree(msg->argv);
		xfree(msg);
	}
}

extern void slurm_free_srun_ping_msg(srun_ping_msg_t * msg)
{
	xfree(msg);
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

extern void slurm_free_checkpoint_msg(checkpoint_msg_t *msg)
{
	if (msg) {
		xfree(msg->image_dir);
		xfree(msg);
	}
}

extern void slurm_free_checkpoint_comp_msg(checkpoint_comp_msg_t *msg)
{
	if (msg) {
		xfree(msg->error_msg);
		xfree(msg);
	}
}

extern void slurm_free_checkpoint_task_comp_msg(checkpoint_task_comp_msg_t *msg)
{
	if (msg) {
		xfree(msg->error_msg);
		xfree(msg);
	}
}

extern void slurm_free_checkpoint_resp_msg(checkpoint_resp_msg_t *msg)
{
	if (msg) {
		xfree(msg->error_msg);
		xfree(msg);
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
	if (msg) {
		xfree(msg->rpc_type_id);
		xfree(msg->rpc_type_cnt);
		xfree(msg->rpc_type_time);
		xfree(msg->rpc_user_id);
		xfree(msg->rpc_user_cnt);
		xfree(msg->rpc_user_time);
		xfree(msg);
	}
}

extern void slurm_free_spank_env_request_msg(spank_env_request_msg_t *msg)
{
	xfree(msg);
}

extern void slurm_free_spank_env_responce_msg(spank_env_responce_msg_t *msg)
{
	uint32_t i;

	for (i = 0; i < msg->spank_job_env_size; i++)
		xfree(msg->spank_job_env[i]);
	xfree(msg->spank_job_env);
	xfree(msg);
}

/* Free job array oriented response with individual return codes by task ID */
extern void slurm_free_job_array_resp(job_array_resp_msg_t *msg)
{
	uint32_t i;

	if (msg) {
		for (i = 0; i < msg->job_array_count; i++)
			xfree(msg->job_array_id[i]);
		xfree(msg->job_array_id);
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
	default:
		snprintf(val, sizeof(val), "%d", inx);
		return val;
	}
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
		for (i = 0; i < msg->host_cnt; i++)
			xfree(msg->kvs_host_ptr[i].hostname);
		xfree(msg->kvs_host_ptr);

		for (i = 0; i < msg->kvs_comm_recs; i++) {
			if (!msg->kvs_comm_ptr[i])
				continue;

			xfree(msg->kvs_comm_ptr[i]->kvs_name);
			for (j = 0; j < msg->kvs_comm_ptr[i]->kvs_cnt; j++) {
				xfree(msg->kvs_comm_ptr[i]->kvs_keys[j]);
				xfree(msg->kvs_comm_ptr[i]->kvs_values[j]);
			}
			xfree(msg->kvs_comm_ptr[i]->kvs_keys);
			xfree(msg->kvs_comm_ptr[i]->kvs_values);
		}
		xfree(msg->kvs_comm_ptr);
		xfree(msg);
	}
}

extern void slurm_free_will_run_response_msg(will_run_response_msg_t *msg)
{
	if (msg) {
		xfree(msg->node_list);
		FREE_NULL_LIST(msg->preemptee_job_id);
		xfree(msg);
	}
}

inline void slurm_free_forward_data_msg(forward_data_msg_t *msg)
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

extern char *preempt_mode_string(uint16_t preempt_mode)
{
	char *gang_str;
	static char preempt_str[64];

	if (preempt_mode == PREEMPT_MODE_OFF)
		return "OFF";
	if (preempt_mode == PREEMPT_MODE_GANG)
		return "GANG";

	if (preempt_mode & PREEMPT_MODE_GANG) {
		gang_str = "GANG,";
		preempt_mode &= (~PREEMPT_MODE_GANG);
	} else
		gang_str = "";

	if      (preempt_mode == PREEMPT_MODE_CANCEL)
		sprintf(preempt_str, "%sCANCEL", gang_str);
	else if (preempt_mode == PREEMPT_MODE_CHECKPOINT)
		sprintf(preempt_str, "%sCHECKPOINT", gang_str);
	else if (preempt_mode == PREEMPT_MODE_REQUEUE)
		sprintf(preempt_str, "%sREQUEUE", gang_str);
	else if (preempt_mode == PREEMPT_MODE_SUSPEND)
		sprintf(preempt_str, "%sSUSPEND", gang_str);
	else
		sprintf(preempt_str, "%sUNKNOWN", gang_str);

	return preempt_str;
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
		} else if ((xstrcasecmp(tok, "off") == 0)
			   || (xstrcasecmp(tok, "cluster") == 0)) {
			mode_num += PREEMPT_MODE_OFF;
			preempt_modes++;
		} else if (xstrcasecmp(tok, "cancel") == 0) {
			mode_num += PREEMPT_MODE_CANCEL;
			preempt_modes++;
		} else if (xstrcasecmp(tok, "checkpoint") == 0) {
			mode_num += PREEMPT_MODE_CHECKPOINT;
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
			mode_num = (uint16_t) NO_VAL;
			break;
		}
		tok = strtok_r(NULL, ",", &last);
	}
	xfree(tmp_str);
	if (preempt_modes > 1) {
		mode_num = (uint16_t) NO_VAL;
	}

	return mode_num;
}

/* Convert log level number to equivalent string */
extern char *log_num2string(uint16_t inx)
{
	switch (inx) {
	case 0:
		return "quiet";
	case 1:
		return "fatal";
	case 2:
		return "error";
	case 3:
		return "info";
	case 4:
		return "verbose";
	case 5:
		return "debug";
	case 6:
		return "debug2";
	case 7:
		return "debug3";
	case 8:
		return "debug4";
	case 9:
		return "debug5";
	default:
		return "unknown";
	}
}

/* Convert log level string to equivalent number */
extern uint16_t log_string2num(char *name)
{
	if (name == NULL)
		return (uint16_t) NO_VAL;

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

	return (uint16_t) NO_VAL;
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
	if (inx & JOB_CONFIGURING)
		return "CONFIGURING";
	if (inx & JOB_RESIZING)
		return "RESIZING";
	if (inx & JOB_REQUEUE)
		return "REQUEUED";
	if (inx & JOB_REQUEUE_HOLD)
		return "REQUEUE_HOLD";
	if (inx & JOB_SPECIAL_EXIT)
		return "SPECIAL_EXIT";
	if (inx & JOB_STOPPED)
		return "STOPPED";


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
	default:
		return "?";
	}
}

extern char *job_state_string_compact(uint32_t inx)
{
	/* Process JOB_STATE_FLAGS */
	if (inx & JOB_COMPLETING)
		return "CG";
	if (inx & JOB_CONFIGURING)
		return "CF";
	if (inx & JOB_RESIZING)
		return "RS";
	if (inx & JOB_REQUEUE)
		return "RQ";
	if (inx & JOB_REQUEUE_HOLD)
		return "RH";
	if (inx & JOB_SPECIAL_EXIT)
		return "SE";
	if (inx & JOB_STOPPED)
		return "ST";

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
	default:
		return "?";
	}
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

	for (i=0; i<JOB_END; i++) {
		if (_job_name_test(i, state_name))
			return i;
	}

	if (_job_name_test(JOB_COMPLETING, state_name))
		return JOB_COMPLETING;
	if (_job_name_test(JOB_CONFIGURING, state_name))
		return JOB_CONFIGURING;
	if (_job_name_test(JOB_RESIZING, state_name))
		return JOB_RESIZING;
	if (_job_name_test(JOB_SPECIAL_EXIT, state_name))
		return JOB_SPECIAL_EXIT;

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

	if (node_state & HEALTH_CHECK_NODE_IDLE)
		if (state_str[0])
			xstrcat(state_str, ",");
		xstrcat(state_str, "IDLE");
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
	else if (trig_type == TRIGGER_TYPE_BLOCK_ERR)
		return "block_err";
	else
		return "unknown";
}

/* user needs to xfree return value */
extern char *reservation_flags_string(uint32_t flags)
{
	char *flag_str = xstrdup("");

	if (flags & RESERVE_FLAG_MAINT)
		xstrcat(flag_str, "MAINT");
	if (flags & RESERVE_FLAG_NO_MAINT) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_MAINT");
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
	if (flags & RESERVE_FLAG_PURGE_COMP) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "PURGE_COMP");
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
	if (priority_flags & PRIORITY_FLAGS_FAIR_TREE) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "FAIR_TREE");
	}
	if (priority_flags & PRIORITY_FLAGS_MAX_TRES) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "MAX_TRES");
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
	if (state == BB_STATE_STAGING_IN)
		return "staging-in";
	if (state == BB_STATE_STAGED_IN)
		return "staged-in";
	if (state == BB_STATE_RUNNING)
		return "running";
	if (state == BB_STATE_SUSPEND)
		return "suspended";
	if (state == BB_STATE_STAGING_OUT)
		return "staging-out";
	if (state == BB_STATE_STAGED_OUT)
		return "staged-out";
	if (state == BB_STATE_TEARDOWN)
		return "teardown";
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
	if (!xstrcasecmp(tok, "staging-in"))
		return BB_STATE_STAGING_IN;
	if (!xstrcasecmp(tok, "staged-in"))
		return BB_STATE_STAGED_IN;
	if (!xstrcasecmp(tok, "running"))
		return BB_STATE_RUNNING;
	if (!xstrcasecmp(tok, "suspend"))
		return BB_STATE_SUSPEND;
	if (!xstrcasecmp(tok, "staging-out"))
		return BB_STATE_STAGING_OUT;
	if (!xstrcasecmp(tok, "staged-out"))
		return BB_STATE_STAGED_OUT;
	if (!xstrcasecmp(tok, "teardown"))
		return BB_STATE_TEARDOWN;
	if (!xstrcasecmp(tok, "complete"))
		return BB_STATE_COMPLETE;
	return 0;
}

extern char *node_state_string(uint32_t inx)
{
	int  base            = (inx & NODE_STATE_BASE);
	bool comp_flag       = (inx & NODE_STATE_COMPLETING);
	bool drain_flag      = (inx & NODE_STATE_DRAIN);
	bool fail_flag       = (inx & NODE_STATE_FAIL);
	bool maint_flag      = (inx & NODE_STATE_MAINT);
	bool net_flag        = (inx & NODE_STATE_NET);
	bool res_flag        = (inx & NODE_STATE_RES);
	bool resume_flag     = (inx & NODE_RESUME);
	bool no_resp_flag    = (inx & NODE_STATE_NO_RESPOND);
	bool power_down_flag = (inx & NODE_STATE_POWER_SAVE);
	bool power_up_flag   = (inx & NODE_STATE_POWER_UP);

	if (maint_flag) {
		if ((base == NODE_STATE_ALLOCATED) ||
		    (base == NODE_STATE_MIXED))
			;
		else if (no_resp_flag)
			return "MAINT*";
		else
			return "MAINT";
	}
	if (drain_flag) {
		if (comp_flag
		    || (base == NODE_STATE_ALLOCATED)
		    || (base == NODE_STATE_MIXED)) {
			if (no_resp_flag)
				return "DRAINING*";
			return "DRAINING";
		} else if (base == NODE_STATE_ERROR) {
			if (no_resp_flag)
				return "ERROR*";
			return "ERROR";
		} else {
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

	if (inx == NODE_STATE_POWER_SAVE)
		return "POWER_DOWN";
	if (inx == NODE_STATE_POWER_UP)
		return "POWER_UP";
	if (base == NODE_STATE_DOWN) {
		if (no_resp_flag)
			return "DOWN*";
		return "DOWN";
	}

	if (base == NODE_STATE_ALLOCATED) {
		if (maint_flag)
			return "ALLOCATED$";
		if (power_up_flag)
			return "ALLOCATED#";
		if (power_down_flag)
			return "ALLOCATED~";
		if (no_resp_flag)
			return "ALLOCATED*";
		if (comp_flag)
			return "ALLOCATED+";
		return "ALLOCATED";
	}
	if (comp_flag) {
		if (no_resp_flag)
			return "COMPLETING*";
		return "COMPLETING";
	}
	if (base == NODE_STATE_IDLE) {
		if (maint_flag)
			return "IDLE$";
		if (power_up_flag)
			return "IDLE#";
		if (power_down_flag)
			return "IDLE~";
		if (no_resp_flag)
			return "IDLE*";
		if (net_flag)
			return "PERFCTRS";
		if (res_flag)
			return "RESERVED";
		return "IDLE";
	}
	if (base == NODE_STATE_ERROR) {
		if (maint_flag)
			return "ERROR$";
		if (power_up_flag)
			return "ERROR#";
		if (power_down_flag)
			return "ERROR~";
		if (no_resp_flag)
			return "ERROR*";
		return "ERROR";
	}
	if (base == NODE_STATE_MIXED) {
		if (maint_flag)
			return "MIXED$";
		if (power_up_flag)
			return "MIXED#";
		if (power_down_flag)
			return "MIXED~";
		if (no_resp_flag)
			return "MIXED*";
		return "MIXED";
	}
	if (base == NODE_STATE_FUTURE) {
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
	bool res_flag        = (inx & NODE_STATE_RES);
	bool resume_flag     = (inx & NODE_RESUME);
	bool no_resp_flag    = (inx & NODE_STATE_NO_RESPOND);
	bool power_down_flag = (inx & NODE_STATE_POWER_SAVE);
	bool power_up_flag   = (inx & NODE_STATE_POWER_UP);

	inx = (inx & NODE_STATE_BASE);

	if (maint_flag) {
		if ((inx == NODE_STATE_ALLOCATED) || (inx == NODE_STATE_MIXED))
			;
		else if (no_resp_flag)
			return "MAINT*";
		else
			return "MAINT";
	}
	if (drain_flag) {
		if (comp_flag
		    || (inx == NODE_STATE_ALLOCATED)
		    || (inx == NODE_STATE_MIXED)) {
			if (no_resp_flag)
				return "DRNG*";
			return "DRNG";
		} else if (inx == NODE_STATE_ERROR) {
			if (no_resp_flag)
				return "ERROR*";
			return "ERROR";
		} else {
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

	if (inx == NODE_STATE_POWER_SAVE)
		return "POW_DN";
	if (inx == NODE_STATE_POWER_UP)
		return "POW_UP";
	if (inx == NODE_STATE_DOWN) {
		if (no_resp_flag)
			return "DOWN*";
		return "DOWN";
	}

	if (inx == NODE_STATE_ALLOCATED) {
		if (power_up_flag)
			return "ALLOC#";
		if (power_down_flag)
			return "ALLOC~";
		if (no_resp_flag)
			return "ALLOC*";
		if (comp_flag)
			return "ALLOC+";
		return "ALLOC";
	}
	if (comp_flag) {
		if (no_resp_flag)
			return "COMP*";
		return "COMP";
	}
	if (inx == NODE_STATE_IDLE) {
		if (maint_flag)
			return "IDLE$";
		if (power_up_flag)
			return "IDLE#";
		if (power_down_flag)
			return "IDLE~";
		if (no_resp_flag)
			return "IDLE*";
		if (net_flag)
			return "NPC";
		if (res_flag)
			return "RESV";
		return "IDLE";
	}
	if (inx == NODE_STATE_ERROR) {
		if (maint_flag)
			return "ERR$";
		if (power_up_flag)
			return "ERR#";
		if (power_down_flag)
			return "ERR~";
		if (no_resp_flag)
			return "ERR*";
		return "ERR";
	}
	if (inx == NODE_STATE_MIXED) {
		if (maint_flag)
			return "MIX$";
		if (power_up_flag)
			return "MIX#";
		if (power_down_flag)
			return "MIX~";
		if (no_resp_flag)
			return "MIX*";
		return "MIX";
	}
	if (inx == NODE_STATE_FUTURE) {
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

extern uint16_t power_flags_id(char *power_flags)
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
	if (str_len < 62) {
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

	// total len 62

	if (str[0] == '\0')
		strcat(str, "none");
}

extern void accounting_enforce_string(uint16_t enforce, char *str, int str_len)
{
	if (str_len > 0)
		str[0] = '\0';
	if (str_len < 30) {
		error("enforce: output buffer too small");
		return;
	}

	if (enforce & ACCOUNTING_ENFORCE_ASSOCS)
		strcat(str, "associations"); //12 len
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

extern char *conn_type_string(enum connection_type conn_type)
{
	switch (conn_type) {
	case (SELECT_MESH):
		return "Mesh";
	case (SELECT_TORUS):
		return "Torus";
	case (SELECT_SMALL):
		return "Small";
	case (SELECT_NAV):
		return "NAV";
	case SELECT_HTC_S:
		return "HTC_S";
	case SELECT_HTC_D:
		return "HTC_D";
	case SELECT_HTC_V:
		return "HTC_V";
	case SELECT_HTC_L:
		return "HTC_L";
	default:
		return "n/a";
	}
	return "n/a";
}

/* caller must xfree after call */
extern char *conn_type_string_full(uint16_t *conn_type)
{
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();

	if ((cluster_flags & CLUSTER_FLAG_BGQ)
	    && (conn_type[0] < SELECT_SMALL)) {
		int dim, pos = 0;
		uint16_t cluster_dims = slurmdb_setup_cluster_dims();
		char conn_type_part[cluster_dims*2], *tmp_char;

		for (dim = 0; dim < cluster_dims; dim++) {
			if (pos)
				conn_type_part[pos++] = ',';
			tmp_char = conn_type_string(conn_type[dim]);
			conn_type_part[pos++] = tmp_char[0];
		}
		conn_type_part[pos] = '\0';
		return xstrdup(conn_type_part);
	} else
		return xstrdup(conn_type_string(conn_type[0]));
}

extern char* node_use_string(enum node_use_type node_use)
{
	switch (node_use) {
	case (SELECT_COPROCESSOR_MODE):
		return "COPROCESSOR";
	case (SELECT_VIRTUAL_NODE_MODE):
		return "VIRTUAL";
	default:
		break;
	}
	return "";
}

extern char *bg_block_state_string(uint16_t state)
{
	static char tmp[25];
	char *state_str = NULL;
	char *err_str = NULL;
	if (state & BG_BLOCK_ERROR_FLAG) {
		err_str = "Error";
		state &= (~BG_BLOCK_ERROR_FLAG);
	}

	switch (state) {
	case BG_BLOCK_NAV:
		if (!err_str)
			state_str = "NAV";
		else {
			err_str = NULL;
			state_str = "Error";
		}
		break;
	case BG_BLOCK_FREE:
		state_str = "Free";
		break;
	case BG_BLOCK_BUSY:
		state_str = "Busy";
		break;
	case BG_BLOCK_BOOTING:
		state_str = "Boot";
		break;
	case BG_BLOCK_REBOOTING:
		state_str = "Reboot";
		break;
	case BG_BLOCK_INITED:
		state_str = "Ready";
		break;
	case BG_BLOCK_ALLOCATED:
		state_str = "Alloc";
		break;
	case BG_BLOCK_TERM:
		state_str = "Term";
		break;
	default:
		state_str = "Unknown";
		break;
	}

	if (err_str)
		snprintf(tmp, sizeof(tmp), "%s(%s)", err_str, state_str);
	else
		return state_str;
	return tmp;
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
	int i;

	if (msg) {
		select_g_select_jobinfo_free(msg->select_jobinfo);
		msg->select_jobinfo = NULL;
		xfree(msg->account);
		xfree(msg->alias_list);
		xfree(msg->cpus_per_node);
		xfree(msg->cpu_count_reps);
		for (i = 0; i < msg->env_size; i++)
			xfree(msg->environment[i]);
		xfree(msg->environment);
		xfree(msg->node_list);
		xfree(msg->partition);
		xfree(msg->qos);
		xfree(msg->resv_name);
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
		xfree(msg->node_addr);
		xfree(msg->node_list);
		delete_sbcast_cred(msg->sbcast_cred);
		xfree(msg);
	}
}

/*
 * slurm_free_job_alloc_info_response_msg - free slurm job allocation
 *					    info response message
 * IN msg - pointer to job allocation info response message
 * NOTE: buffer is loaded by slurm_allocate_resources
 */
extern void slurm_free_job_alloc_info_response_msg(
		job_alloc_info_response_msg_t *msg)
{
	if (msg) {
		if (msg->select_jobinfo)
			select_g_select_jobinfo_free(msg->select_jobinfo);
		xfree(msg->node_list);
		xfree(msg->cpus_per_node);
		xfree(msg->cpu_count_reps);
		xfree(msg->node_addr);
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
	xfree(msg);
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

extern void slurm_free_job_step_info_members (job_step_info_t * msg)
{
	if (msg != NULL) {
		xfree(msg->ckpt_dir);
		xfree(msg->gres);
		xfree(msg->name);
		xfree(msg->network);
		xfree(msg->nodes);
		xfree(msg->node_inx);
		xfree(msg->partition);
		xfree(msg->resv_ports);
		select_g_select_jobinfo_free(msg->select_jobinfo);
		msg->select_jobinfo = NULL;
		xfree(msg->tres_alloc_str);
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
		xfree(node->reason);
		select_g_select_nodeinfo_free(node->select_nodeinfo);
		node->select_nodeinfo = NULL;
		xfree(node->version);
		xfree(node->tres_fmt_str);
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
		xfree(part->deny_accounts);
		xfree(part->deny_qos);
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
	if (resv) {
		xfree(resv->accounts);
		xfree(resv->burst_buffer);
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
		for (i = 0; i < msg->record_count; i++) {
			xfree(msg->topo_array[i].name);
			xfree(msg->topo_array[i].nodes);
			xfree(msg->topo_array[i].switches);
		}
		xfree(msg->topo_array);
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

/*
 * slurm_free_powercap_info_msg - free the powercapping information
 *	response message
 * IN msg - pointer to powercapping information response message
 * NOTE: buffer is loaded by slurm_load_powercap.
 */
extern void slurm_free_powercap_info_msg(powercap_info_msg_t *msg)
{
	xfree(msg);
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

extern void slurm_free_block_job_info(void *object)
{
	block_job_info_t *block_job_info = (block_job_info_t *)object;
	if (block_job_info) {
		xfree(block_job_info->cnodes);
		xfree(block_job_info->cnode_inx);
		xfree(block_job_info->user_name);
		xfree(block_job_info);
	}
}

extern void slurm_free_block_info_members(block_info_t *block_info)
{
	if (block_info) {
		xfree(block_info->bg_block_id);
		xfree(block_info->blrtsimage);
		xfree(block_info->ionode_inx);
		xfree(block_info->ionode_str);
		xfree(block_info->linuximage);
		xfree(block_info->mloaderimage);
		xfree(block_info->mp_inx);
		xfree(block_info->mp_str);
		xfree(block_info->ramdiskimage);
		xfree(block_info->reason);
	}
}

extern void slurm_free_block_info(block_info_t *block_info)
{
	if (block_info) {
		slurm_free_block_info_members(block_info);
		xfree(block_info);
	}
}

extern void slurm_free_block_info_msg(block_info_msg_t *block_info_msg)
{
	if (block_info_msg) {
		if (block_info_msg->block_array) {
			int i;
			for(i=0; i<block_info_msg->record_count; i++)
				slurm_free_block_info_members(
					&(block_info_msg->block_array[i]));
			xfree(block_info_msg->block_array);
		}
		xfree(block_info_msg);
	}
}

extern void slurm_free_block_info_request_msg(
	block_info_request_msg_t *msg)
{
	xfree(msg);
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

extern int slurm_free_msg_data(slurm_msg_type_t type, void *data)
{
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
	case REQUEST_COMPLETE_BATCH_JOB:
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
	case RESPONSE_ACCT_GATHER_UPDATE:
		slurm_free_acct_gather_node_resp_msg(data);
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
	case MESSAGE_NODE_REGISTRATION_STATUS:
		slurm_free_node_registration_status_msg(data);
		break;
	case REQUEST_JOB_END_TIME:
	case REQUEST_JOB_ALLOCATION_INFO:
	case REQUEST_JOB_ALLOCATION_INFO_LITE:
		slurm_free_job_alloc_info_msg(data);
		break;
	case REQUEST_JOB_SBCAST_CRED:
		slurm_free_step_alloc_info_msg(data);
		break;
	case REQUEST_SHUTDOWN:
		slurm_free_shutdown_msg(data);
		break;
	case REQUEST_UPDATE_FRONT_END:
		slurm_free_update_front_end_msg(data);
		break;
	case REQUEST_UPDATE_NODE:
		slurm_free_update_node_msg(data);
		break;
	case REQUEST_UPDATE_LAYOUT:
		slurm_free_update_layout_msg(data);
		break;
	case REQUEST_CREATE_PARTITION:
	case REQUEST_UPDATE_PARTITION:
		slurm_free_update_part_msg(data);
		break;
	case REQUEST_UPDATE_POWERCAP:
		slurm_free_powercap_info_msg(data);
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
	case REQUEST_LAYOUT_INFO:
		slurm_free_layout_info_request_msg(data);
		break;
	case REQUEST_CHECKPOINT:
		slurm_free_checkpoint_msg(data);
		break;
	case REQUEST_CHECKPOINT_COMP:
		slurm_free_checkpoint_comp_msg(data);
		break;
	case REQUEST_CHECKPOINT_TASK_COMP:
		slurm_free_checkpoint_task_comp_msg(data);
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
	case REQUEST_JOB_REQUEUE:
		slurm_free_requeue_msg(data);
		break;
	case REQUEST_JOB_READY:
	case REQUEST_JOB_INFO_SINGLE:
		slurm_free_job_id_msg(data);
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
	case REQUEST_BLOCK_INFO:
		slurm_free_block_info_request_msg(data);
		break;
	case REQUEST_STEP_COMPLETE:
	case REQUEST_STEP_COMPLETE_AGGR:
		slurm_free_step_complete_msg(data);
		break;
	case REQUEST_JOB_STEP_STAT:
	case REQUEST_JOB_STEP_PIDS:
	case REQUEST_STEP_LAYOUT:
		slurm_free_job_step_id_msg(data);
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
	case TASK_USER_MANAGED_IO_STREAM:
		slurm_free_task_user_managed_io_stream_msg(data);
		break;
	case REQUEST_SIGNAL_TASKS:
	case REQUEST_TERMINATE_TASKS:
		slurm_free_kill_tasks_msg(data);
		break;
	case REQUEST_CHECKPOINT_TASKS:
		slurm_free_checkpoint_tasks_msg(data);
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
	case REQUEST_SIGNAL_JOB:
		slurm_free_signal_job_msg(data);
		break;
	case REQUEST_ABORT_JOB:
	case REQUEST_TERMINATE_JOB:
		slurm_free_kill_job_msg(data);
		break;
	case REQUEST_UPDATE_JOB_TIME:
		slurm_free_update_job_time_msg(data);
		break;
	case REQUEST_JOB_ID:
		slurm_free_job_id_request_msg(data);
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
	case REQUEST_TAKEOVER:
	case REQUEST_SHUTDOWN_IMMEDIATE:
	case RESPONSE_FORWARD_FAILED:
	case REQUEST_DAEMON_STATUS:
	case REQUEST_HEALTH_CHECK:
	case REQUEST_ACCT_GATHER_UPDATE:
	case ACCOUNTING_FIRST_REG:
	case REQUEST_TOPO_INFO:
	case REQUEST_BURST_BUFFER_INFO:
	case REQUEST_POWERCAP_INFO:
	case ACCOUNTING_REGISTER_CTLD:
		/* No body to free */
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
	case REQUEST_SPANK_ENVIRONMENT:
		slurm_free_spank_env_request_msg(data);
		break;
	case RESPONCE_SPANK_ENVIRONMENT:
		slurm_free_spank_env_responce_msg(data);
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
	case MESSAGE_COMPOSITE:
	case RESPONSE_MESSAGE_COMPOSITE:
		slurm_free_composite_msg(data);
		break;
	case REQUEST_UPDATE_BLOCK:
		slurm_free_block_info(data);
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
	case SRUN_EXEC:
		slurm_free_srun_exec_msg(data);
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
	case PMI_KVS_GET_REQ:
		slurm_free_get_kvs_msg(data);
		break;
	case PMI_KVS_GET_RESP:
	case PMI_KVS_PUT_REQ:
		slurm_free_kvs_comm_set(data);
		break;
	case PMI_KVS_PUT_RESP:
		/* No body */
		break;
	case RESPONSE_RESOURCE_ALLOCATION:
		slurm_free_resource_allocation_response_msg(data);
		break;
	case REQUEST_ASSOC_MGR_INFO:
		slurm_free_assoc_mgr_info_request_msg(data);
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

	switch(type) {
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

	for (cc = 0; cc < msg->num_lic; cc++) {
		xfree(msg->lic_array[cc].name);
	}
	xfree(msg->lic_array);
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
	case RESPONSE_RECONFIGURE:
		return "RESPONSE_RECONFIGURE";
	case REQUEST_SHUTDOWN:
		return "REQUEST_SHUTDOWN";
	case REQUEST_SHUTDOWN_IMMEDIATE:
		return "REQUEST_SHUTDOWN_IMMEDIATE";
	case RESPONSE_SHUTDOWN:
		return "RESPONSE_SHUTDOWN";
	case REQUEST_PING:
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
	case REQUEST_ACCTING_INFO:
		return "REQUEST_ACCTING_INFO";
	case RESPONSE_ACCOUNTING_INFO:
		return "RESPONSE_ACCOUNTING_INFO";
	case REQUEST_JOB_ID:
		return "REQUEST_JOB_ID";
	case RESPONSE_JOB_ID:
		return "RESPONSE_JOB_ID";
	case REQUEST_BLOCK_INFO:
		return "REQUEST_BLOCK_INFO";
	case RESPONSE_BLOCK_INFO:
		return "RESPONSE_BLOCK_INFO";
	case REQUEST_TRIGGER_SET:
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
	case REQUEST_SPANK_ENVIRONMENT:
		return "REQUEST_SPANK_ENVIRONMENT";
	case RESPONCE_SPANK_ENVIRONMENT:
		return "RESPONCE_SPANK_ENVIRONMENT";
	case REQUEST_STATS_INFO:
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
	case REQUEST_POWERCAP_INFO:
		return "REQUEST_POWERCAP_INFO";
	case RESPONSE_POWERCAP_INFO:
		return "RESPONSE_POWERCAP_INFO";
	case REQUEST_ASSOC_MGR_INFO:
		return "REQUEST_ASSOC_MGR_INFO";
	case RESPONSE_ASSOC_MGR_INFO:
		return "RESPONSE_ASSOC_MGR_INFO";
	/* case REQUEST_SICP_INFO_DEFUNCT:			DEFUNCT */
	/* case RESPONSE_SICP_INFO_DEFUNCT:			DEFUNCT */
	case REQUEST_LAYOUT_INFO:
		return "REQUEST_LAYOUT_INFO";
	case RESPONSE_LAYOUT_INFO:
		return "RESPONSE_LAYOUT_INFO";

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
	case REQUEST_UPDATE_BLOCK:
		return "REQUEST_UPDATE_BLOCK";			/* 3010 */
	case REQUEST_UPDATE_FRONT_END:
		return "REQUEST_UPDATE_FRONT_END";
	case REQUEST_UPDATE_LAYOUT:
		return "REQUEST_UPDATE_LAYOUT";
	case REQUEST_UPDATE_POWERCAP:
		return "REQUEST_UPDATE_POWERCAP";

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
	case RESPONSE_CANCEL_JOB:
		return "RESPONSE_CANCEL_JOB";
	case REQUEST_JOB_RESOURCE:
		return "REQUEST_JOB_RESOURCE";
	case RESPONSE_JOB_RESOURCE:
		return "RESPONSE_JOB_RESOURCE";
	case REQUEST_JOB_ATTACH:				/* 4010 */
		return "REQUEST_JOB_ATTACH";
	case RESPONSE_JOB_ATTACH:
		return "RESPONSE_JOB_ATTACH";
	case REQUEST_JOB_WILL_RUN:
		return "REQUEST_JOB_WILL_RUN";
	case RESPONSE_JOB_WILL_RUN:
		return "RESPONSE_JOB_WILL_RUN";
	case REQUEST_JOB_ALLOCATION_INFO:
		return "REQUEST_JOB_ALLOCATION_INFO";
	case RESPONSE_JOB_ALLOCATION_INFO:
		return "RESPONSE_JOB_ALLOCATION_INFO";
	case REQUEST_JOB_ALLOCATION_INFO_LITE:
		return "REQUEST_JOB_ALLOCATION_INFO_LITE";
	case RESPONSE_JOB_ALLOCATION_INFO_LITE:
		return "RESPONSE_JOB_ALLOCATION_INFO_LITE";
	case REQUEST_UPDATE_JOB_TIME:
		return "REQUEST_UPDATE_JOB_TIME";
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

	case REQUEST_JOB_STEP_CREATE:				/* 5001 */
		return "REQUEST_JOB_STEP_CREATE";
	case RESPONSE_JOB_STEP_CREATE:
		return "RESPONSE_JOB_STEP_CREATE";
	case REQUEST_RUN_JOB_STEP:
		return "REQUEST_RUN_JOB_STEP";
	case RESPONSE_RUN_JOB_STEP:
		return "RESPONSE_RUN_JOB_STEP";
	case REQUEST_CANCEL_JOB_STEP:
		return "REQUEST_CANCEL_JOB_STEP";
	case RESPONSE_CANCEL_JOB_STEP:
		return "RESPONSE_CANCEL_JOB_STEP";
	case REQUEST_UPDATE_JOB_STEP:
		return "REQUEST_UPDATE_JOB_STEP";
	/* case DEFUNCT_RESPONSE_COMPLETE_JOB_STEP:		DEFUNCT */
	case REQUEST_CHECKPOINT:
		return "REQUEST_CHECKPOINT";
	case RESPONSE_CHECKPOINT:				/* 5010 */
		return "RESPONSE_CHECKPOINT";
	case REQUEST_CHECKPOINT_COMP:
		return "REQUEST_CHECKPOINT_COMP";
	case REQUEST_CHECKPOINT_TASK_COMP:
		return "REQUEST_CHECKPOINT_TASK_COMP";
	case RESPONSE_CHECKPOINT_COMP:
		return "RESPONSE_CHECKPOINT_COMP";
	case REQUEST_SUSPEND:
		return "REQUEST_SUSPEND";
	case RESPONSE_SUSPEND:
		return "RESPONSE_SUSPEND";
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
	case RESPONSE_SLURMCTLD_STATUS:
		return "RESPONSE_SLURMCTLD_STATUS";
	case REQUEST_JOB_STEP_PIDS:
		return "REQUEST_JOB_STEP_PIDS";
	case RESPONSE_JOB_STEP_PIDS:
		return "RESPONSE_JOB_STEP_PIDS";
	case REQUEST_FORWARD_DATA:
		return "REQUEST_FORWARD_DATA";
	case REQUEST_COMPLETE_BATCH_JOB:			/* 5030 */
		return "REQUEST_COMPLETE_BATCH_JOB";
	case REQUEST_SUSPEND_INT:
		return "REQUEST_SUSPEND_INT";
	case REQUEST_KILL_JOB:
		return "REQUEST_KILL_JOB";
	case REQUEST_KILL_JOBSTEP:
		return "REQUEST_KILL_JOBSTEP";
	case RESPONSE_JOB_ARRAY_ERRORS:
		return "RESPONSE_JOB_ARRAY_ERRORS";
	case REQUEST_NETWORK_CALLERID:
		return "REQUEST_NETWORK_CALLERID";
	case RESPONSE_NETWORK_CALLERID:
		return "RESPONSE_NETWORK_CALLERID";
	case REQUEST_STEP_COMPLETE_AGGR:
		return "REQUEST_STEP_COMPLETE_AGGR";
	case REQUEST_TOP_JOB:
		return "REQUEST_TOP_JOB";

	case REQUEST_LAUNCH_TASKS:				/* 6001 */
		return "REQUEST_LAUNCH_TASKS";
	case RESPONSE_LAUNCH_TASKS:
		return "RESPONSE_LAUNCH_TASKS";
	case MESSAGE_TASK_EXIT:
		return "MESSAGE_TASK_EXIT";
	case REQUEST_SIGNAL_TASKS:
		return "REQUEST_SIGNAL_TASKS";
	case REQUEST_CHECKPOINT_TASKS:
		return "REQUEST_CHECKPOINT_TASKS";
	case REQUEST_TERMINATE_TASKS:
		return "REQUEST_TERMINATE_TASKS";
	case REQUEST_REATTACH_TASKS:
		return "REQUEST_REATTACH_TASKS";
	case RESPONSE_REATTACH_TASKS:
		return "RESPONSE_REATTACH_TASKS";
	case REQUEST_KILL_TIMELIMIT:
		return "REQUEST_KILL_TIMELIMIT";
	case REQUEST_SIGNAL_JOB:				/* 6010 */
		return "REQUEST_SIGNAL_JOB";
	case REQUEST_TERMINATE_JOB:
		return "REQUEST_TERMINATE_JOB";
	case MESSAGE_EPILOG_COMPLETE:
		return "MESSAGE_EPILOG_COMPLETE";
	case REQUEST_ABORT_JOB:
		return "REQUEST_ABORT_JOB";
	case REQUEST_FILE_BCAST:
		return "REQUEST_FILE_BCAST";
	case TASK_USER_MANAGED_IO_STREAM:
		return "TASK_USER_MANAGED_IO_STREAM";
	case REQUEST_KILL_PREEMPTED:
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
	case SRUN_EXEC:
		return "SRUN_EXEC";
	case SRUN_STEP_MISSING:
		return "SRUN_STEP_MISSING";
	case SRUN_REQUEST_SUSPEND:
		return "SRUN_REQUEST_SUSPEND";
	case SRUN_STEP_SIGNAL:
		return "SRUN_STEP_SIGNAL";

	case PMI_KVS_PUT_REQ:					/* 7201 */
		return "PMI_KVS_PUT_REQ";
	case PMI_KVS_PUT_RESP:
		return "PMI_KVS_PUT_RESP";
	case PMI_KVS_GET_REQ:
		return "PMI_KVS_GET_REQ";
	case PMI_KVS_GET_RESP:
		return "PMI_KVS_GET_RESP";

	case RESPONSE_SLURM_RC:					/* 8001 */
		return "RESPONSE_SLURM_RC";
	case RESPONSE_SLURM_RC_MSG:
		return "RESPONSE_SLURM_RC_MSG";

	case RESPONSE_FORWARD_FAILED:				/* 9001 */
		return "RESPONSE_FORWARD_FAILED";

	case ACCOUNTING_UPDATE_MSG:				/* 10001 */
		return "ACCOUNTING_UPDATE_MSG";
	case ACCOUNTING_FIRST_REG:
		return "ACCOUNTING_FIRST_REG";
	case ACCOUNTING_REGISTER_CTLD:
		return "ACCOUNTING_REGISTER_CTLD";

	case MESSAGE_COMPOSITE:					/* 110001 */
		return "MESSAGE_COMPOSITE";
	case RESPONSE_MESSAGE_COMPOSITE:
		return "RESPONSE_MESSAGE_COMPOSITE";

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

extern uint32_t
slurm_bb_str2flags(char *bb_str)
{
	uint32_t bb_flags = 0;

	if (bb_str && strstr(bb_str, "DisablePersistent"))
		bb_flags |= BB_FLAG_DISABLE_PERSISTENT;
	if (bb_str && strstr(bb_str, "EmulateCray"))
		bb_flags |= BB_FLAG_EMULATE_CRAY;
	if (bb_str && strstr(bb_str, "EnablePersistent"))
		bb_flags |= BB_FLAG_ENABLE_PERSISTENT;
	if (bb_str && strstr(bb_str, "PrivateData"))
		bb_flags |= BB_FLAG_PRIVATE_DATA;
	if (bb_str && strstr(bb_str, "TeardownFailure"))
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
		rc = SLURM_FAILURE;
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
