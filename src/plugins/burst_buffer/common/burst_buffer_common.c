/*****************************************************************************\
 *  burst_buffer_common.c - Common logic for managing burst_buffers
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#define _GNU_SOURCE	/* For POLLRDHUP */
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#include "burst_buffer_common.h"


/* Translate colon delimitted list of users into a UID array,
 * Return value must be xfreed */
static uid_t *_parse_users(char *buf)
{
	char *delim, *tmp, *tok, *save_ptr = NULL;
	int inx = 0, array_size;
	uid_t *user_array = NULL;

	if (!buf)
		return user_array;
	tmp = xstrdup(buf);
	delim = strchr(tmp, ',');
	if (delim)
		delim[0] = '\0';
	array_size = 1;
	user_array = xmalloc(sizeof(uid_t) * array_size);
	tok = strtok_r(tmp, ":", &save_ptr);
	while (tok) {
		if ((uid_from_string(tok, user_array + inx) == -1) ||
		    (user_array[inx] == 0)) {
			error("%s: ignoring invalid user: %s", __func__, tok);
		} else {
			if (++inx >= array_size) {
				array_size *= 2;
				user_array = xrealloc(user_array,
						      sizeof(uid_t)*array_size);
			}
		}
		tok = strtok_r(NULL, ":", &save_ptr);
	}
	xfree(tmp);
	return user_array;
}

/* Translate an array of (zero terminated) UIDs into a string with colon
 * delimited UIDs
 * Return value must be xfreed */
static char *_print_users(uid_t *buf)
{
	char *user_elem, *user_str = NULL;
	int i;

	if (!buf)
		return user_str;
	for (i = 0; buf[i]; i++) {
		user_elem = uid_to_string(buf[i]);
		if (!user_elem)
			continue;
		if (user_str)
			xstrcat(user_str, ":");
		xstrcat(user_str, user_elem);
		xfree(user_elem);
	}
	return user_str;
}

/* Clear configuration parameters, free memory */
extern void bb_clear_config(bb_config_t *config_ptr)
{
	xassert(config_ptr);
	xfree(config_ptr->allow_users);
	xfree(config_ptr->allow_users_str);
	config_ptr->debug_flag = false;
	xfree(config_ptr->deny_users);
	xfree(config_ptr->deny_users_str);
	xfree(config_ptr->get_sys_state);
	config_ptr->job_size_limit = NO_VAL;
	config_ptr->stage_in_timeout = 0;
	config_ptr->stage_out_timeout = 0;
	config_ptr->prio_boost_alloc = 0;
	config_ptr->prio_boost_use = 0;
	xfree(config_ptr->start_stage_in);
	xfree(config_ptr->start_stage_out);
	xfree(config_ptr->stop_stage_in);
	xfree(config_ptr->stop_stage_out);
	config_ptr->user_size_limit = NO_VAL;
}

/* Load and process configuration parameters */
extern void bb_load_config(bb_config_t *config_ptr)
{
	s_p_hashtbl_t *bb_hashtbl = NULL;
	char *bb_conf, *tmp = NULL, *value;
	static s_p_options_t bb_options[] = {
		{"AllowUsers", S_P_STRING},
		{"DenyUsers", S_P_STRING},
		{"GetSysState", S_P_STRING},
		{"JobSizeLimit", S_P_STRING},
		{"PrioBoostAlloc", S_P_UINT32},
		{"PrioBoostUse", S_P_UINT32},
		{"StageInTimeout", S_P_UINT32},
		{"StageOutTimeout", S_P_UINT32},
		{"StartStageIn", S_P_STRING},
		{"StartStageOut", S_P_STRING},
		{"StopStageIn", S_P_STRING},
		{"StopStageOut", S_P_STRING},
		{"UserSizeLimit", S_P_STRING},
		{NULL}
	};

	bb_clear_config(config_ptr);
	if (slurm_get_debug_flags() & DEBUG_FLAG_BURST_BUF)
		config_ptr->debug_flag = true;

	bb_conf = get_extra_conf_path("burst_buffer.conf");
	bb_hashtbl = s_p_hashtbl_create(bb_options);
	if (s_p_parse_file(bb_hashtbl, NULL, bb_conf, false) == SLURM_ERROR) {
		fatal("%s: something wrong with opening/reading %s: %m",
		      __func__, bb_conf);
	}
	if (s_p_get_string(&config_ptr->allow_users_str, "AllowUsers",
			   bb_hashtbl)) {
		config_ptr->allow_users = _parse_users(config_ptr->
						       allow_users_str);
	}
	if (s_p_get_string(&config_ptr->deny_users_str, "DenyUsers",
			   bb_hashtbl)) {
		config_ptr->deny_users = _parse_users(config_ptr->
						      deny_users_str);
	}
	if (!s_p_get_string(&config_ptr->get_sys_state, "GetSysState",
			    bb_hashtbl))
		fatal("%s: GetSysState is NULL", __func__);
	if (s_p_get_string(&tmp, "JobSizeLimit", bb_hashtbl)) {
		config_ptr->job_size_limit = bb_get_size_num(tmp);
		xfree(tmp);
	}
	if (s_p_get_uint32(&config_ptr->prio_boost_alloc, "PrioBoostAlloc",
			   bb_hashtbl) &&
	    (config_ptr->prio_boost_alloc > NICE_OFFSET)) {
		error("%s: PrioBoostAlloc can not exceed %u",
		      __func__, NICE_OFFSET);
		config_ptr->prio_boost_alloc = NICE_OFFSET;
	}
	if (s_p_get_uint32(&config_ptr->prio_boost_use, "PrioBoostUse",
			   bb_hashtbl) &&
	    (config_ptr->prio_boost_use > NICE_OFFSET)) {
		error("%s: PrioBoostUse can not exceed %u",
		      __func__, NICE_OFFSET);
		config_ptr->prio_boost_use = NICE_OFFSET;
	}
	s_p_get_uint32(&config_ptr->stage_in_timeout, "StageInTimeout",
		       bb_hashtbl);
	s_p_get_uint32(&config_ptr->stage_out_timeout, "StageOutTimeout",
		       bb_hashtbl);
	if (!s_p_get_string(&config_ptr->start_stage_in, "StartStageIn",
			    bb_hashtbl))
		fatal("%s: StartStageIn is NULL", __func__);
	if (!s_p_get_string(&config_ptr->start_stage_out, "StartStageOut",
			    bb_hashtbl))
		fatal("%s: StartStageOut is NULL", __func__);
	if (!s_p_get_string(&config_ptr->stop_stage_in, "StopStageIn",
			    bb_hashtbl))
		fatal("%s: StopStageIn is NULL", __func__);
	if (!s_p_get_string(&config_ptr->stop_stage_out, "StopStageOut",
			    bb_hashtbl))
		fatal("%s: StopStageOut is NULL", __func__);
	if (s_p_get_string(&tmp, "UserSizeLimit", bb_hashtbl)) {
		config_ptr->user_size_limit = bb_get_size_num(tmp);
		xfree(tmp);
	}

	s_p_hashtbl_destroy(bb_hashtbl);
	xfree(bb_conf);

	if (config_ptr->debug_flag) {
		value = _print_users(config_ptr->allow_users);
		info("%s: AllowUsers:%s",  __func__, value);
		xfree(value);

		value = _print_users(config_ptr->deny_users);
		info("%s: DenyUsers:%s",  __func__, value);
		xfree(value);

		info("%s: GetSysState:%s",  __func__,
		     config_ptr->get_sys_state);
		info("%s: JobSizeLimit:%u",  __func__,
		     config_ptr->job_size_limit);
		info("%s: PrioBoostAlloc:%u", __func__,
		     config_ptr->prio_boost_alloc);
		info("%s: PrioBoostUse:%u", __func__,
		     config_ptr->prio_boost_use);
		info("%s: StageInTimeout:%u", __func__,
		     config_ptr->stage_in_timeout);
		info("%s: StageOutTimeout:%u", __func__,
		     config_ptr->stage_out_timeout);
		info("%s: StartStageIn:%s",  __func__,
		     config_ptr->start_stage_in);
		info("%s: StartStageOut:%s",  __func__,
		     config_ptr->start_stage_out);
		info("%s: StopStageIn:%s",  __func__,
		     config_ptr->stop_stage_in);
		info("%s: StopStageOut:%s",  __func__,
		     config_ptr->stop_stage_out);
		info("%s: UserSizeLimit:%u",  __func__,
		     config_ptr->user_size_limit);
	}
}

/* Pack configuration parameters into a buffer */
extern void bb_pack_config(bb_config_t *config_ptr, Buf buffer,
			   uint16_t protocol_version)
{
	packstr(config_ptr->allow_users_str, buffer);
	packstr(config_ptr->deny_users_str,  buffer);
	packstr(config_ptr->get_sys_state,   buffer);
	packstr(config_ptr->start_stage_in,  buffer);
	packstr(config_ptr->start_stage_out, buffer);
	packstr(config_ptr->stop_stage_in,   buffer);
	packstr(config_ptr->stop_stage_out,  buffer);
	pack32(config_ptr->job_size_limit,   buffer);
	pack32(config_ptr->prio_boost_alloc, buffer);
	pack32(config_ptr->prio_boost_use,   buffer);
	pack32(config_ptr->stage_in_timeout, buffer);
	pack32(config_ptr->stage_out_timeout,buffer);
	pack32(config_ptr->user_size_limit,  buffer);
}

/* Translate a burst buffer size specification in string form to numeric form,
 * recognizing various sufficies (MB, GB, TB, PB, and Nodes). */
extern uint32_t bb_get_size_num(char *tok)
{
	char *end_ptr = NULL;
	int32_t bb_size_i;
	uint32_t bb_size_u = 0;

	bb_size_i = strtol(tok, &end_ptr, 10);
	if (bb_size_i > 0) {
		bb_size_u = (uint32_t) bb_size_i;
		if ((end_ptr[0] == 'm') || (end_ptr[0] == 'M')) {
			bb_size_u = (bb_size_u + 1023) / 1024;
		} else if ((end_ptr[0] == 'g') || (end_ptr[0] == 'G')) {
			;
		} else if ((end_ptr[0] == 't') || (end_ptr[0] == 'T')) {
			bb_size_u *= 1024;
		} else if ((end_ptr[0] == 'p') || (end_ptr[0] == 'P')) {
			bb_size_u *= (1024 * 1024);
		} else if ((end_ptr[0] == 'n') || (end_ptr[0] == 'N')) {
			bb_size_u |= BB_SIZE_IN_NODES;
		}
	}
	return bb_size_u;
}
