/*****************************************************************************\
 *  burst_buffer_none.c - No-op library for managing a burst_buffer
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
#include <stdlib.h>

#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

#define _DEBUG 1

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "burst_buffer" for SLURM burst_buffer) and <method> is a
 * description of how this plugin satisfies that application.  SLURM will only
 * load a burst_buffer plugin if the plugin_type string has a prefix of
 * "burst_buffer/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "burst_buffer NONE plugin";
const char plugin_type[]        = "burst_buffer/none";
const uint32_t plugin_version   = 100;

#if _DEBUG
static uid_t   *allow_users = NULL;
static uid_t   *deny_users = NULL;
static uint32_t job_size_limit = NO_VAL;
static uint32_t total_space = 0;
static uint32_t user_size_limit = NO_VAL;

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

/* Translate an array of UIDs into a string with colon delimited UIDs
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

static void _clear_config(void)
{
	xfree(allow_users);
	xfree(deny_users);
	job_size_limit = NO_VAL;
	total_space = 0;
	user_size_limit = NO_VAL;
}

/* Load and process BurstBufferParameters configuration parameter */
static void _load_config_params(void)
{
	char *bb_params, *key, *value;

	_clear_config();
	bb_params = slurm_get_bb_params();
	if (bb_params) {
		/*                       01234567890123456 */
		key = strstr(bb_params, "allow_users=");
		if (key)
			allow_users = _parse_users(key + 12);
		value = _print_users(allow_users);
		info("%s: allow_users:%s",  __func__, value);
		xfree(value);

		key = strstr(bb_params, "deny_users=");
		if (allow_users && key) {
			error("%s: ignoring deny_users, allow_users is set",
			      __func__);
		} else if (key) {
			deny_users = _parse_users(key + 11);
		}
		value = _print_users(deny_users);
		info("%s: deny_users:%s",  __func__, value);
		xfree(value);

		key = strstr(bb_params, "job_size_limit=");
		if (key)
			job_size_limit = atoi(key + 15);
		info("%s: job_size_limit:%u",  __func__, job_size_limit);

		key = strstr(bb_params, "user_size_limit=");
		if (key)
			user_size_limit = atoi(key + 16);
		info("%s: user_size_limit:%u",  __func__, user_size_limit);
	}
	xfree(bb_params);
}

static void _load_state(void)
{
	total_space = 1000;	/* For testing purposes only */
	info("%s: total_space:%u",  __func__, total_space);
}
#endif

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
#if _DEBUG
	info("%s: %s",  __func__, plugin_type);
	_load_config_params();
	_load_state();
#endif
	return SLURM_SUCCESS;
}

extern int fini(void)
{
#if _DEBUG
	info("%s: %s",  __func__, plugin_type);
	_clear_config();
#endif
	return SLURM_SUCCESS;
}

extern int bb_p_load_state(void)
{
#if _DEBUG
	info("%s: %s",  __func__, plugin_type);
	_load_state();
#endif
	return SLURM_SUCCESS;
}

extern int bb_p_reconfig(void)
{
#if _DEBUG
	info("%s: %s",  __func__, plugin_type);
	_load_config_params();
#endif
	return SLURM_SUCCESS;
}

extern int bb_p_job_validate(struct job_descriptor *job_desc,
			     uid_t submit_uid)
{
#if _DEBUG
	uint32_t bb_size = 0;
	char *key;
	int i;

	info("%s: job_user_id:%u, submit_uid:%d", __func__,
	     job_desc->user_id, submit_uid);
	info("%s: burst_buffer:%s", __func__, job_desc->burst_buffer);
	info("%s: script:%s", __func__, job_desc->script);

	if (job_desc->burst_buffer) {
		key = strstr(job_desc->burst_buffer, "size=");
		if (key)
			bb_size = atoi(key + 5);
	}
	if (bb_size == 0)
		return SLURM_SUCCESS;
	if ((job_size_limit != NO_VAL) && (bb_size > job_size_limit))
		return ESLURM_BURST_BUFFER_LIMIT;
	if ((user_size_limit != NO_VAL) && (bb_size > user_size_limit))
		return ESLURM_BURST_BUFFER_LIMIT;
	if (allow_users) {
		for (i = 0; allow_users[i]; i++) {
			if (job_desc->user_id == allow_users[i])
				break;
		}
		if (allow_users[i] == 0)
			return ESLURM_BURST_BUFFER_PERMISSION;
	}
	if (deny_users) {
		for (i = 0; deny_users[i]; i++) {
			if (job_desc->user_id == deny_users[i])
				break;
		}
		if (deny_users[i] != 0)
			return ESLURM_BURST_BUFFER_PERMISSION;
	}
	if (bb_size > total_space) {
		info("Job from user %u requested burst buffer size of %u, "
		     "but total space is only %u",
		     job_desc->user_id, bb_size, total_space);
	}
#endif
	return SLURM_SUCCESS;
}
