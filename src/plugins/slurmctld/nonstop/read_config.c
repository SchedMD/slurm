/*****************************************************************************\
 *  read_config.c - Read configuration file for slurmctld/nonstop plugin
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/slurm_xlator.h"	/* Must be first */

#include "src/common/parse_config.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/plugins/slurmctld/nonstop/read_config.h"

/* Global variables */
int hot_spare_info_cnt;
spare_node_resv_t *hot_spare_info;
char *hot_spare_count_str = NULL;
uint16_t nonstop_comm_port = 0;
uint16_t nonstop_debug = 0;
char *nonstop_control_addr = NULL;
char *nonstop_backup_addr = NULL;
uint32_t max_spare_node_count = 0;
uint16_t time_limit_delay = 0;
uint16_t time_limit_drop = 0;
uint16_t time_limit_extend = 0;
int user_drain_allow_cnt = 0;
uid_t *user_drain_allow = NULL;
char *user_drain_allow_str = NULL;
int user_drain_deny_cnt = 0;
uid_t *user_drain_deny = NULL;
char *user_drain_deny_str = NULL;
/* Library variables controlling timeouts with
 * the controller.
 */
uint32_t read_timeout = 0;

uint32_t write_timeout = 0;
munge_ctx_t ctx = NULL;

static s_p_options_t nonstop_options[] = {
	{"BackupAddr", S_P_STRING},
	{"ControlAddr", S_P_STRING},
	{"Debug", S_P_UINT16},
	{"HotSpareCount", S_P_STRING},
	{"MaxSpareNodeCount", S_P_UINT32},
	{"Port", S_P_UINT16},
	{"TimeLimitDelay", S_P_UINT16},
	{"TimeLimitDrop", S_P_UINT16},
	{"TimeLimitExtend", S_P_UINT16},
	{"UserDrainAllow", S_P_STRING},
	{"UserDrainDeny", S_P_STRING},
	{"ReadTimeout", S_P_UINT32},
	{"WriteTimeout", S_P_UINT32},
	{NULL}
};

static void _print_config(void)
{
	char *tmp_str = NULL;
	int i;

	info("select/nonstop plugin configuration");
	info("ControlAddr=%s",		nonstop_control_addr);
	info("BackupAddr=%s",		nonstop_backup_addr);
	info("Debug=%hu",		nonstop_debug);
	if ((nonstop_debug > 1) && hot_spare_info_cnt) {
		for (i = 0; i < hot_spare_info_cnt; i++) {
			if (i)
				xstrcat(tmp_str, ",");
			xstrfmtcat(tmp_str, "%s:%u",
				   hot_spare_info[i].partition,
				   hot_spare_info[i].node_cnt);
		}
		info("HotSpareCount=%s", tmp_str);
		xfree(tmp_str);
	} else {
		info("HotSpareCount=%s",	hot_spare_count_str);
	}
	info("MaxSpareNodeCount=%u",	max_spare_node_count);
	info("Port=%hu",		nonstop_comm_port);
	info("TimeLimitDelay=%hu",	time_limit_delay);
	info("TimeLimitDrop=%hu",	time_limit_drop);
	info("TimeLimitExtend=%hu",	time_limit_extend);
	info("UserDrainAllow=%s",	user_drain_allow_str);
	if ((nonstop_debug > 1) && user_drain_allow_cnt) {
		for (i = 0; i < user_drain_allow_cnt; i++) {
			if (i)
				xstrcat(tmp_str, ",");
			xstrfmtcat(tmp_str, "%u",(uint32_t)user_drain_allow[i]);
		}
		info("UserDrainAllow(UIDs)=%s", tmp_str);
		xfree(tmp_str);
	}
	info("UserDrainDeny=%s",	user_drain_deny_str);
	if ((nonstop_debug > 1) && user_drain_deny_cnt) {
		for (i = 0; i < user_drain_deny_cnt; i++) {
			if (i)
				xstrcat(tmp_str, ",");
			xstrfmtcat(tmp_str, "%u", (uint32_t)user_drain_deny[i]);
		}
		info("UserDrainDeny(UIDs)=%s", tmp_str);
		xfree(tmp_str);
	}
	info("ReadTimeout=%u",	read_timeout);
	info("WriteTimeout=%u",	write_timeout);
}

static spare_node_resv_t *_xlate_hot_spares(char *spare_str, int *spare_cnt)
{
	char *tok, *tmp_str, *save_ptr = NULL;
	char *part, *sep;
	int i, node_cnt = 0;
	spare_node_resv_t *spare_ptr = NULL;
	part_record_t *part_ptr = NULL;
	/* Locks: Read partition */
	slurmctld_lock_t part_read_lock =
	    { NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };

	*spare_cnt = 0;
	if ((spare_str == NULL) || (spare_str[0] == '\0'))
		return spare_ptr;

	tmp_str = xstrdup(spare_str);
	tok = strtok_r(tmp_str, ",", &save_ptr);
	lock_slurmctld(part_read_lock);
	while (tok) {
		static bool dup = false;
		part = xstrdup(tok);
		sep = strchr(part, ':');
		if (sep) {
			node_cnt = atoi(sep + 1);
			sep[0] = '\0';
			part_ptr = find_part_record(part);
			if ((*spare_cnt > 0) && (spare_ptr == NULL)) {
				/* Avoid CLANG error */
				fatal("%s: spare array is NULL with size=%d",
				      __func__, *spare_cnt);
				return spare_ptr;
			}
			for (i = 0; i < *spare_cnt; i++) {
				if (spare_ptr[i].part_ptr != part_ptr)
					continue;
				dup = true;
				break;
			}
		}
		if ((sep == NULL) || (node_cnt < 0)) {
			error("nonstop.conf: Ignoring invalid HotSpare (%s)",
			      tok);
		} else if (dup) {
			info("nonstop.conf: Ignoring HotSpare (%s): "
			     "Duplicate partition record", tok);
		} else if (node_cnt == 0) {
			info("nonstop.conf: Ignoring HotSpare (%s): "
			     "Node count is zero", tok);
		} else if (part_ptr == NULL) {
			error("nonstop.conf: Ignoring invalid HotSpare (%s):"
			      "Partition not found", tok);
		} else {
			xrealloc(spare_ptr, (sizeof(spare_node_resv_t) *
					    (*spare_cnt + 1)));
			spare_ptr[*spare_cnt].node_cnt = node_cnt;
			spare_ptr[*spare_cnt].partition = part;
			part = NULL;	/* Nothing left to free */
			spare_ptr[*spare_cnt].part_ptr = part_ptr;

			(*spare_cnt)++;
		}
		xfree(part);
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	unlock_slurmctld(part_read_lock);
	xfree(tmp_str);

	return spare_ptr;
}

static uid_t *_xlate_users(char *user_str, int *user_cnt)
{
	char *tok, *tmp_str, *save_ptr = NULL;
	uid_t uid, *uid_ptr = NULL;

	*user_cnt = 0;
	if ((user_str == NULL) || (user_str[0] == '\0'))
		return uid_ptr;

	tmp_str = xstrdup(user_str);
	tok = strtok_r(tmp_str, ",", &save_ptr);
	while (tok) {
		int rc = 0;
		if (!xstrcasecmp(tok, "ALL"))
			uid = NO_VAL;
		else
			rc = uid_from_string(tok, &uid);
		if (rc < 0) {
			error("nonstop.conf: Invalid user: %s", tok);
		} else {
			xrealloc(uid_ptr, (sizeof(uid_t) * (*user_cnt + 1)));
			uid_ptr[*user_cnt] = uid;
			(*user_cnt)++;
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp_str);
	return uid_ptr;
}

static void _validate_config(void)
{
	hot_spare_info = _xlate_hot_spares(hot_spare_count_str,
					   &hot_spare_info_cnt);

	user_drain_deny  = _xlate_users(user_drain_deny_str,
					&user_drain_deny_cnt);
	if (user_drain_deny) {
		if (!user_drain_allow_str)
			user_drain_allow_str = xstrdup("ALL");
		if (xstrcasecmp(user_drain_allow_str, "ALL"))
			fatal("nonstop.conf: Bad UserDrainAllow/Deny values");
	}
	user_drain_allow = _xlate_users(user_drain_allow_str,
					&user_drain_allow_cnt);

	if ((ctx = munge_ctx_create()) == NULL)
		fatal("nonstop.conf: munge_ctx_create failed");
}

/* Load configuration file contents into global variables.
 * Call nonstop_free_config to free memory. */
extern void nonstop_read_config(void)
{
	char *nonstop_file = NULL;
	s_p_hashtbl_t *tbl = NULL;
	struct stat config_stat;

	nonstop_file = get_extra_conf_path("nonstop.conf");
	if (stat(nonstop_file, &config_stat) < 0)
		fatal("Can't stat nonstop.conf %s: %m", nonstop_file);
	tbl = s_p_hashtbl_create(nonstop_options);
	if (s_p_parse_file(tbl, NULL, nonstop_file, false, NULL) == SLURM_ERROR)
		fatal("Can't parse nonstop.conf %s: %m", nonstop_file);

	s_p_get_string(&nonstop_backup_addr, "BackupAddr", tbl);
	if (!s_p_get_string(&nonstop_control_addr, "ControlAddr", tbl))
		fatal("No ControlAddr in nonstop.conf %s", nonstop_file);
	s_p_get_uint16(&nonstop_debug, "Debug", tbl);
	s_p_get_string(&hot_spare_count_str, "HotSpareCount", tbl);
	s_p_get_uint32(&max_spare_node_count, "MaxSpareNodeCount", tbl);
	if (!s_p_get_uint16(&nonstop_comm_port, "Port", tbl))
		nonstop_comm_port = DEFAULT_NONSTOP_PORT;
	s_p_get_uint16(&time_limit_delay, "TimeLimitDelay", tbl);
	s_p_get_uint16(&time_limit_drop, "TimeLimitDrop", tbl);
	s_p_get_uint16(&time_limit_extend, "TimeLimitExtend", tbl);
	s_p_get_string(&user_drain_allow_str, "UserDrainAllow", tbl);
	s_p_get_string(&user_drain_deny_str, "UserDrainDeny", tbl);
	s_p_get_uint32(&read_timeout, "ReadTimeout", tbl);
	s_p_get_uint32(&write_timeout, "WriteTimeout", tbl);

	_validate_config();
	if (nonstop_debug > 0)
		_print_config();

	s_p_hashtbl_destroy(tbl);
	xfree(nonstop_file);
}

extern void nonstop_free_config(void)
{
	int i;

	for (i = 0; i < hot_spare_info_cnt; i++)
		xfree(hot_spare_info[i].partition);
	hot_spare_info_cnt = 0;
	xfree(hot_spare_info);
	nonstop_comm_port = 0;
	nonstop_debug = 0;
	xfree(nonstop_control_addr);
	xfree(nonstop_backup_addr);
	xfree(hot_spare_count_str);
	max_spare_node_count = 0;
	time_limit_delay = 0;
	time_limit_drop = 0;
	time_limit_extend = 0;
	user_drain_allow_cnt = 0;
	xfree(user_drain_allow);
	xfree(user_drain_allow_str);
	user_drain_deny_cnt = 0;
	xfree(user_drain_deny);
	xfree(user_drain_deny_str);
	munge_ctx_destroy(ctx);
	ctx = NULL;
}

/* Create reservations to contain hot-spare nodes
 * and purge vestigial reservations */
extern void create_hot_spare_resv(void)
{
	int i;
	char resv_name[1024];
	ListIterator part_iterator;
	part_record_t *part_ptr;
	/* Locks: Read partition */
	slurmctld_lock_t part_read_lock =
	    { NO_LOCK, NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK };
	reservation_name_msg_t delete_resv_msg;
	resv_desc_msg_t resv_msg;
	time_t now = time(NULL);
	uint32_t node_cnt[2];

	lock_slurmctld(part_read_lock);
	part_iterator = list_iterator_create(part_list);
	while ((part_ptr = list_next(part_iterator))) {
		snprintf(resv_name, sizeof(resv_name), "HOT_SPARE_%s",
			 part_ptr->name);
		for (i = 0; i < hot_spare_info_cnt; i++) {
			if (hot_spare_info[i].part_ptr != part_ptr)
				continue;
			memset(&resv_msg, 0, sizeof(resv_msg));
			node_cnt[0] = hot_spare_info[i].node_cnt;
			node_cnt[1] = 0;
			resv_msg.duration	= 356 * 24 * 60 * 60;
			resv_msg.end_time	= (time_t) NO_VAL;
			resv_msg.flags		= RESERVE_FLAG_MAINT |
						  RESERVE_FLAG_IGN_JOBS;
			resv_msg.name		= resv_name;
			resv_msg.node_cnt	= node_cnt;
			resv_msg.partition	= xstrdup(part_ptr->name);
			resv_msg.start_time	= now;
			resv_msg.users		= xstrdup("root");
			if (find_resv_name(resv_name)) {
				info("Updating vestigial reservation %s",
				      resv_name);
				(void) update_resv(&resv_msg);
			} else {
				info("Creating vestigial reservation %s",
				     resv_name);
				(void) create_resv(&resv_msg);
			}
			xfree(resv_msg.partition);
			xfree(resv_msg.users);
			break;
		}
		if ((i >= hot_spare_info_cnt) && find_resv_name(resv_name)) {
			info("Deleting vestigial reservation %s", resv_name);
			memset(&delete_resv_msg, 0, sizeof(delete_resv_msg));
			delete_resv_msg.name = resv_name;
			(void) delete_resv(&delete_resv_msg);
		}
	}
	list_iterator_destroy(part_iterator);
	unlock_slurmctld(part_read_lock);
}

extern void nonstop_read_config_list(List data)
{
	config_key_pair_t *key_pair;
	char *tmp_str = NULL;
	int i;

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("BackupAddr");
	key_pair->value = xstrdup(nonstop_backup_addr);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ControlAddr");
	key_pair->value = xstrdup(nonstop_control_addr);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("Debug");
	key_pair->value = xstrdup_printf("%hu",nonstop_debug);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("HotSpareCount");
	if ((nonstop_debug > 1) && hot_spare_info_cnt) {
		for (i = 0; i < hot_spare_info_cnt; i++) {
			if (i)
				xstrcat(tmp_str, ",");
			xstrfmtcat(tmp_str, "%s:%u",
				   hot_spare_info[i].partition,
				   hot_spare_info[i].node_cnt);
		}
		key_pair->value = xstrdup(tmp_str);
		xfree(tmp_str);
	} else {
		key_pair->value = xstrdup(hot_spare_count_str);
	}
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("MaxSpareNodeCount");
	key_pair->value = xstrdup_printf("%u", max_spare_node_count);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("Port");
	key_pair->value = xstrdup_printf("%hu", nonstop_comm_port);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("ReadTimeout");
	key_pair->value = xstrdup_printf("%u", read_timeout);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TimeLimitDelay");
	key_pair->value = xstrdup_printf("%hu", time_limit_delay);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TimeLimitDrop");
	key_pair->value = xstrdup_printf("%hu", time_limit_drop);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("TimeLimitExtend");
	key_pair->value = xstrdup_printf("%hu", time_limit_extend);
	list_append(data, key_pair);

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("UserDrainAllow");
	key_pair->value = xstrdup(user_drain_allow_str);
	list_append(data, key_pair);

	if ((nonstop_debug > 1) && user_drain_allow_cnt) {
		for (i = 0; i < user_drain_allow_cnt; i++) {
			if (i)
				xstrcat(tmp_str, ",");
			xstrfmtcat(tmp_str, "%u",(uint32_t)user_drain_allow[i]);
		}
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("UserDrainAllow(UIDs)");
		key_pair->value = xstrdup(tmp_str);
		list_append(data, key_pair);
		xfree(tmp_str);
	}

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("UserDrainDeny");
	key_pair->value = xstrdup(user_drain_deny_str);
	list_append(data, key_pair);

	if ((nonstop_debug > 1) && user_drain_deny_cnt) {
		for (i = 0; i < user_drain_deny_cnt; i++) {
			if (i)
				xstrcat(tmp_str, ",");
			xstrfmtcat(tmp_str, "%u", (uint32_t)user_drain_deny[i]);
		}
		key_pair = xmalloc(sizeof(config_key_pair_t));
		key_pair->name = xstrdup("UserDrainDeny(UIDs)");
		key_pair->value = xstrdup(tmp_str);
		list_append(data, key_pair);
		xfree(tmp_str);
	}

	key_pair = xmalloc(sizeof(config_key_pair_t));
	key_pair->name = xstrdup("WriteTimeout");
	key_pair->value = xstrdup_printf("%u", write_timeout);
	list_append(data, key_pair);

	return;
}
