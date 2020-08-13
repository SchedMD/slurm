/*****************************************************************************\
 *  job_submit_lua.c - Set defaults in job submit request specifications.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2017 SchedMD LLC <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include <dlfcn.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/common/assoc_mgr.h"
#include "src/common/uid.h"
#include "src/lua/slurm_lua.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/reservation.h"

#define _DEBUG 0
#define MIN_ACCTG_FREQUENCY 30

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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]       	= "Job submit lua plugin";
const char plugin_type[]       	= "job_submit/lua";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

static const char lua_script_path[] = DEFAULT_SCRIPT_DIR "/job_submit.lua";
static time_t lua_script_last_loaded = (time_t) 0;
static lua_State *L = NULL;
static char *user_msg = NULL;
time_t last_lua_jobs_update = (time_t) 0;
time_t last_lua_resv_update = (time_t) 0;
static const char *req_fxns[] = {
	"slurm_job_submit",
	"slurm_job_modify",
	NULL
};
/*
 *  Mutex for protecting multi-threaded access to this plugin.
 *   (Only 1 thread at a time should be in here)
 */
static pthread_mutex_t lua_lock = PTHREAD_MUTEX_INITIALIZER;

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern uint16_t accounting_enforce __attribute__((weak_import));
extern void *acct_db_conn  __attribute__((weak_import));
#else
uint16_t accounting_enforce = 0;
void *acct_db_conn = NULL;
#endif

/*****************************************************************************\
 * We've provided a simple example of the type of things you can do with this
 * plugin. If you develop another plugin that may be of interest to others
 * please post it to slurm-dev@schedmd.com  Thanks!
\*****************************************************************************/

/* Get the default account for a user (or NULL if not present) */
static char *_get_default_account(uint32_t user_id)
{
	slurmdb_user_rec_t user;

	memset(&user, 0, sizeof(slurmdb_user_rec_t));
	user.uid = user_id;
	if (assoc_mgr_fill_in_user(acct_db_conn, &user, accounting_enforce,
				   NULL, false) != SLURM_ERROR) {
		return user.default_acct;
	} else {
		return NULL;
	}
}

/* Get the default QOS for an association (or NULL if not present) */
static char *_get_default_qos(uint32_t user_id, char *account, char *partition)
{
	slurmdb_assoc_rec_t assoc;
	slurmdb_qos_rec_t qos;
	uint32_t qos_id = 0;

	memset(&assoc, 0, sizeof(slurmdb_assoc_rec_t));
	assoc.uid = user_id;
	assoc.partition = partition;
	if (account) {
		assoc.acct = account;
	} else {
		assoc.acct = _get_default_account(user_id);
	}

	if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc, accounting_enforce,
				    NULL, false) != SLURM_ERROR)
		qos_id = assoc.def_qos_id;

	if (!qos_id)
		return NULL;

	memset(&qos, 0, sizeof(slurmdb_qos_rec_t));
	qos.id = qos_id;
	if (assoc_mgr_fill_in_qos(acct_db_conn, &qos, accounting_enforce,
				  NULL, false) != SLURM_ERROR) {
		return qos.name;
	} else {
		return NULL;
	}
}

/* Get fields in an existing slurmctld job_record */
static int _job_rec_field_index(lua_State *L)
{
	const char *name = luaL_checkstring(L, 2);
	job_record_t *job_ptr;

	lua_getmetatable(L, -2);
	lua_getfield(L, -1, "_job_rec_ptr");
	job_ptr = lua_touserdata(L, -1);

	return slurm_lua_job_record_field(L, job_ptr, name);
}

/* Get the list of existing slurmctld job records. */
static void _update_jobs_global(lua_State *st)
{
	char job_id_buf[11]; /* Big enough for a uint32_t */
	ListIterator iter;
	job_record_t *job_ptr;

	if (last_lua_jobs_update >= last_job_update) {
		return;
	}

	lua_getglobal(st, "slurm");
	lua_newtable(st);

	iter = list_iterator_create(job_list);
	while ((job_ptr = list_next(iter))) {
		/* Create an empty table, with a metatable that looks up the
		 * data for the individual job.
		 */
		lua_newtable(st);

		lua_newtable(st);
		lua_pushcfunction(st, _job_rec_field_index);
		lua_setfield(st, -2, "__index");
		/* Store the job_record in the metatable, so the index
		 * function knows which job it's getting data for.
		 */
		lua_pushlightuserdata(st, job_ptr);
		lua_setfield(st, -2, "_job_rec_ptr");
		lua_setmetatable(st, -2);

		/* Lua copies passed strings, so we can reuse the buffer. */
		snprintf(job_id_buf, sizeof(job_id_buf),
		         "%d", job_ptr->job_id);
		lua_setfield(st, -2, job_id_buf);
	}
	last_lua_jobs_update = last_job_update;
	list_iterator_destroy(iter);

	lua_setfield(st, -2, "jobs");
	lua_pop(st, 1);
}

static int _resv_field(const slurmctld_resv_t *resv_ptr,
                       const char *name)
{
	if (resv_ptr == NULL) {
		error("_resv_field: resv_ptr is NULL");
		lua_pushnil(L);
	} else if (!xstrcmp(name, "accounts")) {
		lua_pushstring(L, resv_ptr->accounts);
	} else if (!xstrcmp(name, "assoc_list")) {
		lua_pushstring(L, resv_ptr->assoc_list);
	} else if (!xstrcmp(name, "duration")) {
		lua_pushnumber(L, resv_ptr->duration);
	} else if (!xstrcmp(name, "end_time")) {
		lua_pushnumber(L, resv_ptr->end_time);
	} else if (!xstrcmp(name, "features")) {
		lua_pushstring(L, resv_ptr->features);
	} else if (!xstrcmp(name, "flags")) {
		lua_pushnumber(L, resv_ptr->flags);
	} else if (!xstrcmp(name, "full_nodes")) {
		lua_pushboolean(L,
				(resv_ptr->ctld_flags & RESV_CTLD_FULL_NODE) ?
				true : false);
	} else if (!xstrcmp(name, "flags_set_node")) {
		lua_pushboolean(L,
				(resv_ptr->ctld_flags &
				 RESV_CTLD_NODE_FLAGS_SET) ? true : false);
	} else if (!xstrcmp(name, "licenses")) {
		lua_pushstring(L, resv_ptr->licenses);
	} else if (!xstrcmp(name, "node_cnt")) {
		lua_pushnumber(L, resv_ptr->node_cnt);
	} else if (!xstrcmp(name, "node_list")) {
		lua_pushstring(L, resv_ptr->node_list);
	} else if (!xstrcmp(name, "partition")) {
		lua_pushstring(L, resv_ptr->partition);
	} else if (!xstrcmp(name, "start_time")) {
		lua_pushnumber(L, resv_ptr->start_time);
	} else if (!xstrcmp(name, "users")) {
		lua_pushstring(L, resv_ptr->users);
	} else {
		lua_pushnil(L);
	}

	return 1;
}

/* Get fields in an existing slurmctld reservation record */
static int _resv_field_index(lua_State *L)
{
	const char *name = luaL_checkstring(L, 2);
	slurmctld_resv_t *resv_ptr;

	lua_getmetatable(L, -2);
	lua_getfield(L, -1, "_resv_ptr");
	resv_ptr = lua_touserdata(L, -1);

	return _resv_field(resv_ptr, name);
}

/* Get the list of existing slurmctld reservation records. */
static void _update_resvs_global(lua_State *st)
{
	ListIterator iter;
	slurmctld_resv_t *resv_ptr;

	if (last_lua_resv_update >= last_resv_update) {
		return;
	}

	lua_getglobal(st, "slurm");
	lua_newtable(st);

	iter = list_iterator_create(resv_list);
	while ((resv_ptr = (slurmctld_resv_t *) list_next(iter))) {
		/* Create an empty table, with a metatable that looks up the
		 * data for the individual reservation.
		 */
		lua_newtable(st);

		lua_newtable(st);
		lua_pushcfunction(st, _resv_field_index);
		lua_setfield(st, -2, "__index");
		/* Store the slurmctld_resv_t in the metatable, so the index
		 * function knows which reservation it's getting data for.
		 */
		lua_pushlightuserdata(st, resv_ptr);
		lua_setfield(st, -2, "_resv_ptr");
		lua_setmetatable(st, -2);

		lua_setfield(st, -2, resv_ptr->name);
	}
	last_lua_resv_update = last_resv_update;
	list_iterator_destroy(iter);

	lua_setfield(st, -2, "reservations");
	lua_pop(st, 1);
}

/* Set fields in the job request structure on job submit or modify */
static int _set_job_env_field(lua_State *L)
{
	const char *name, *value_str;
	job_desc_msg_t *job_desc;
	char *name_eq = NULL;
	int i, j, name_len;

	name = luaL_checkstring(L, 2);
	name_eq = xstrdup(name);
	xstrcat(name_eq, "=");
	name_len = strlen(name_eq);
	lua_getmetatable(L, -3);
	lua_getfield(L, -1, "_job_desc");
	job_desc = lua_touserdata(L, -1);
	if (job_desc == NULL) {
		error("%s: job_desc is NULL", __func__);
	} else if (job_desc->environment == NULL) {
		error("%s: job_desc->environment is NULL", __func__);
		lua_pushnil(L);
	} else {
		value_str = luaL_checkstring(L, 3);
		for (i = 0; job_desc->environment[i]; i++) {
			if (!xstrncmp(job_desc->environment[i], name_eq,
				      name_len)) {
				job_desc->environment[i][name_len] = '\0';
				xstrcat(job_desc->environment[i], value_str);
				break;
			}
		}
		if (!job_desc->environment[i]) {
			job_desc->environment = xrealloc(job_desc->environment,
							 sizeof(char*) * (i+2));
			for (j = i; j >= 1; j--) {
				job_desc->environment[j] =
					job_desc->environment[j-1];
			}
			job_desc->environment[0] = xstrdup(name_eq);
			xstrcat(job_desc->environment[0], value_str);
			job_desc->env_size++;
		}
	}
	xfree(name_eq);

	return 0;
}

static int _job_env_field(const job_desc_msg_t *job_desc, const char *name)
{
	char *name_eq = "";
	int i, name_len;

	name_eq = xstrdup(name);
	xstrcat(name_eq, "=");
	name_len = strlen(name_eq);
	if (job_desc == NULL) {
		error("%s: job_desc is NULL", __func__);
		lua_pushnil(L);
	} else if (job_desc->environment == NULL) {
		error("%s: job_desc->environment is NULL", __func__);
		lua_pushnil(L);
	} else {
		for (i = 0; job_desc->environment[i]; i++) {
			if (!xstrncmp(job_desc->environment[i], name_eq,
				      name_len)) {
				lua_pushstring(L, job_desc->environment[i] +
					       name_len);
				break;
			}
		}
		if (!job_desc->environment[i])
			lua_pushnil(L);
	}
	xfree(name_eq);

	return 1;
}

/* Get fields in the job request record on job submit or modify */
static int _get_job_env_field_name(lua_State *L)
{
	const job_desc_msg_t *job_desc = lua_touserdata(L, 1);
	const char *name = luaL_checkstring(L, 2);
	return _job_env_field(job_desc, name);
}

/* Get fields in an existing slurmctld job_descriptor record */
static int _job_env_field_index(lua_State *L)
{
	const char *name;
	job_desc_msg_t *job_desc;

	name = luaL_checkstring(L, 2);
	lua_getmetatable(L, -2);
	lua_getfield(L, -1, "_job_desc");
	job_desc = lua_touserdata(L, -1);
	return _job_env_field(job_desc, name);
}

static void _push_job_env(job_desc_msg_t *job_desc)
{
	lua_newtable(L);

	lua_newtable(L);
	lua_pushcfunction(L, _job_env_field_index);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, _set_job_env_field);
	lua_setfield(L, -2, "__newindex");
	/* Store the job descriptor in the metatable, so the index
	 * function knows which struct it's getting data for.
	 */
	lua_pushlightuserdata(L, job_desc);
	lua_setfield(L, -2, "_job_desc");
	lua_setmetatable(L, -2);
}

static int _get_job_req_field(const job_desc_msg_t *job_desc, const char *name)
{
	int i;

	if (job_desc == NULL) {
		error("%s: job_desc is NULL", __func__);
		lua_pushnil(L);
	} else if (!xstrcmp(name, "account")) {
		lua_pushstring(L, job_desc->account);
	} else if (!xstrcmp(name, "acctg_freq")) {
		lua_pushstring(L, job_desc->acctg_freq);
	} else if (!xstrcmp(name, "admin_comment")) {
		lua_pushstring(L, job_desc->admin_comment);
	} else if (!xstrcmp(name, "alloc_node")) {
		lua_pushstring(L, job_desc->alloc_node);
	} else if (!xstrcmp(name, "argc")) {
		lua_pushnumber(L, job_desc->argc);
	} else if (!xstrcmp(name, "argv")) {
		if ((job_desc->argc == 0) ||
		    (job_desc->argv == NULL)) {
			lua_pushnil(L);
		} else {
			lua_newtable(L);
			for (i = 0; i < job_desc->argc; i++) {
				if (job_desc->argv[i] != NULL) {
					lua_pushnumber(L, i);
					lua_pushstring(L, job_desc->argv[i]);
					lua_settable(L, -3);
				}
			}
		}
	} else if (!xstrcmp(name, "array_inx")) {
		lua_pushstring(L, job_desc->array_inx);
	} else if (!xstrcmp(name, "batch_features")) {
		lua_pushstring(L, job_desc->batch_features);
	} else if (!xstrcmp(name, "begin_time")) {
		lua_pushnumber(L, job_desc->begin_time);
	} else if (!xstrcmp(name, "bitflags")) {
		lua_pushnumber(L, job_desc->bitflags);
	} else if (!xstrcmp(name, "boards_per_node")) {
		lua_pushnumber(L, job_desc->boards_per_node);
	} else if (!xstrcmp(name, "burst_buffer")) {
		lua_pushstring(L, job_desc->burst_buffer);
	} else if (!xstrcmp(name, "clusters")) {
		lua_pushstring(L, job_desc->clusters);
	} else if (!xstrcmp(name, "comment")) {
		lua_pushstring(L, job_desc->comment);
	} else if (!xstrcmp(name, "contiguous")) {
		lua_pushnumber(L, job_desc->contiguous);
	} else if (!xstrcmp(name, "cores_per_socket")) {
		lua_pushnumber(L, job_desc->cores_per_socket);
	} else if (!xstrcmp(name, "cpu_freq_min")) {
		lua_pushnumber(L, job_desc->cpu_freq_min);
	} else if (!xstrcmp(name, "cpu_freq_max")) {
		lua_pushnumber(L, job_desc->cpu_freq_max);
	} else if (!xstrcmp(name, "cpu_freq_gov")) {
		lua_pushnumber(L, job_desc->cpu_freq_gov);
	} else if (!xstrcmp(name, "cpus_per_task")) {
		lua_pushnumber(L, job_desc->cpus_per_task);
	} else if (!xstrcmp(name, "cpus_per_tres")) {
		lua_pushstring(L, job_desc->cpus_per_tres);
	} else if (!xstrcmp(name, "default_account")) {
		lua_pushstring(L, _get_default_account(job_desc->user_id));
	} else if (!xstrcmp(name, "default_qos")) {
		lua_pushstring(L, _get_default_qos(job_desc->user_id,
						   job_desc->account,
						   job_desc->partition));
	} else if (!xstrcmp(name, "delay_boot")) {
		lua_pushnumber(L, job_desc->delay_boot);
	} else if (!xstrcmp(name, "dependency")) {
		lua_pushstring(L, job_desc->dependency);
	} else if (!xstrcmp(name, "end_time")) {
		lua_pushnumber(L, job_desc->end_time);
	} else if (!xstrcmp(name, "environment")) {
		_push_job_env((job_desc_msg_t *) job_desc); // No const
	} else if (!xstrcmp(name, "extra")) {
		lua_pushstring(L, job_desc->extra);
	} else if (!xstrcmp(name, "exc_nodes")) {
		lua_pushstring(L, job_desc->exc_nodes);
	} else if (!xstrcmp(name, "features")) {
		lua_pushstring(L, job_desc->features);
	} else if (!xstrcmp(name, "gres")) {
		/* "gres" replaced by "tres_per_node" in v18.08 */
		lua_pushstring(L, job_desc->tres_per_node);
	} else if (!xstrcmp(name, "group_id")) {
		lua_pushnumber(L, job_desc->group_id);
	} else if (!xstrcmp(name, "immediate")) {
		lua_pushnumber(L, job_desc->immediate);
	} else if (!xstrcmp(name, "licenses")) {
		lua_pushstring(L, job_desc->licenses);
	} else if (!xstrcmp(name, "mail_type")) {
		lua_pushnumber(L, job_desc->mail_type);
	} else if (!xstrcmp(name, "mail_user")) {
		lua_pushstring(L, job_desc->mail_user);
	} else if (!xstrcmp(name, "max_cpus")) {
		lua_pushnumber(L, job_desc->max_cpus);
	} else if (!xstrcmp(name, "max_nodes")) {
		lua_pushnumber(L, job_desc->max_nodes);
	} else if (!xstrcmp(name, "mem_per_tres")) {
		lua_pushstring(L, job_desc->mem_per_tres);
	} else if (!xstrcmp(name, "min_cpus")) {
		lua_pushnumber(L, job_desc->min_cpus);
	} else if (!xstrcmp(name, "min_mem_per_node") &&
		   (job_desc->pn_min_memory != NO_VAL64) &&
		   !(job_desc->pn_min_memory & MEM_PER_CPU)) {
		lua_pushnumber(L, job_desc->pn_min_memory);
	} else if (!xstrcmp(name, "min_mem_per_cpu") &&
		   (job_desc->pn_min_memory != NO_VAL64) &&
		   (job_desc->pn_min_memory & MEM_PER_CPU)) {
		lua_pushnumber(L, (job_desc->pn_min_memory & (~MEM_PER_CPU)));
	} else if (!xstrcmp(name, "min_nodes")) {
		lua_pushnumber(L, job_desc->min_nodes);
	} else if (!xstrcmp(name, "name")) {
		lua_pushstring(L, job_desc->name);
	} else if (!xstrcmp(name, "nice")) {
		lua_pushnumber(L, job_desc->nice);
	} else if (!xstrcmp(name, "ntasks_per_board")) {
		lua_pushnumber(L, job_desc->ntasks_per_board);
	} else if (!xstrcmp(name, "ntasks_per_core")) {
		lua_pushnumber(L, job_desc->ntasks_per_core);
	} else if (!xstrcmp(name, "ntasks_per_node")) {
		lua_pushnumber(L, job_desc->ntasks_per_node);
	} else if (!xstrcmp(name, "ntasks_per_socket")) {
		lua_pushnumber(L, job_desc->ntasks_per_socket);
	} else if (!xstrcmp(name, "num_tasks")) {
		lua_pushnumber(L, job_desc->num_tasks);
		/* Continue support for old hetjob terminology. */
	} else if (!xstrcmp(name, "pack_job_offset") ||
		   !xstrcmp(name, "het_job_offset")) {
		lua_pushnumber(L, job_desc->het_job_offset);
	} else if (!xstrcmp(name, "partition")) {
		lua_pushstring(L, job_desc->partition);
	} else if (!xstrcmp(name, "power_flags")) {
		lua_pushnumber(L, job_desc->power_flags);
	} else if (!xstrcmp(name, "pn_min_cpus")) {
		lua_pushnumber(L, job_desc->pn_min_cpus);
	} else if (!xstrcmp(name, "pn_min_memory")) {
		/*
		 * FIXME: Remove this in the future, lua can't handle 64bit
		 * numbers!!!.  Use min_mem_per_node|cpu instead.
		 */
		lua_pushnumber(L, job_desc->pn_min_memory);
	} else if (!xstrcmp(name, "pn_min_tmp_disk")) {
		lua_pushnumber(L, job_desc->pn_min_tmp_disk);
	} else if (!xstrcmp(name, "priority")) {
		lua_pushnumber(L, job_desc->priority);
	} else if (!xstrcmp(name, "qos")) {
		lua_pushstring(L, job_desc->qos);
	} else if (!xstrcmp(name, "reboot")) {
		lua_pushnumber(L, job_desc->reboot);
	} else if (!xstrcmp(name, "req_nodes")) {
		lua_pushstring(L, job_desc->req_nodes);
	} else if (!xstrcmp(name, "req_switch")) {
		lua_pushnumber(L, job_desc->req_switch);
	} else if (!xstrcmp(name, "requeue")) {
		lua_pushnumber(L, job_desc->requeue);
	} else if (!xstrcmp(name, "reservation")) {
		lua_pushstring(L, job_desc->reservation);
	} else if (!xstrcmp(name, "script")) {
		lua_pushstring(L, job_desc->script);
	} else if (!xstrcmp(name, "shared")) {
		lua_pushnumber(L, job_desc->shared);
	} else if (!xstrcmp(name, "site_factor")) {
		if (job_desc->site_factor == NO_VAL)
			lua_pushnumber(L, job_desc->site_factor);
		else
			lua_pushnumber(L,
				       (((int64_t)job_desc->site_factor)
					- NICE_OFFSET));
	} else if (!xstrcmp(name, "sockets_per_board")) {
		lua_pushnumber(L, job_desc->sockets_per_board);
	} else if (!xstrcmp(name, "sockets_per_node")) {
		lua_pushnumber(L, job_desc->sockets_per_node);
	} else if (!xstrcmp(name, "spank_job_env")) {
		if ((job_desc->spank_job_env_size == 0) ||
		    (job_desc->spank_job_env == NULL)) {
			lua_pushnil(L);
		} else {
			lua_newtable(L);
			for (i = 0; i < job_desc->spank_job_env_size; i++) {
				if (job_desc->spank_job_env[i] != NULL) {
					lua_pushnumber(L, i);
					lua_pushstring(
						L, job_desc->spank_job_env[i]);
					lua_settable(L, -3);
				}
			}
		}
	} else if (!xstrcmp(name, "spank_job_env_size")) {
		lua_pushnumber(L, job_desc->spank_job_env_size);
	} else if (!xstrcmp(name, "std_err")) {
		lua_pushstring(L, job_desc->std_err);
	} else if (!xstrcmp(name, "std_in")) {
		lua_pushstring(L, job_desc->std_in);
	} else if (!xstrcmp(name, "std_out")) {
		lua_pushstring(L, job_desc->std_out);
	} else if (!xstrcmp(name, "threads_per_core")) {
		lua_pushnumber(L, job_desc->threads_per_core);
	} else if (!xstrcmp(name, "time_limit")) {
		lua_pushnumber(L, job_desc->time_limit);
	} else if (!xstrcmp(name, "time_min")) {
		lua_pushnumber(L, job_desc->time_min);
	} else if (!xstrcmp(name, "tres_bind")) {
		lua_pushstring(L, job_desc->tres_bind);
	} else if (!xstrcmp(name, "tres_freq")) {
		lua_pushstring(L, job_desc->tres_freq);
	} else if (!xstrcmp(name, "tres_per_job")) {
		lua_pushstring(L, job_desc->tres_per_job);
	} else if (!xstrcmp(name, "tres_per_node")) {
		lua_pushstring(L, job_desc->tres_per_node);
	} else if (!xstrcmp(name, "tres_per_socket")) {
		lua_pushstring(L, job_desc->tres_per_socket);
	} else if (!xstrcmp(name, "tres_per_task")) {
		lua_pushstring(L, job_desc->tres_per_task);
	} else if (!xstrcmp(name, "user_id")) {
		lua_pushnumber(L, job_desc->user_id);
	} else if (!xstrcmp(name, "user_name")) {
		char *username = uid_to_string_or_null(job_desc->user_id);
		lua_pushstring(L, username);
		xfree(username);
	} else if (!xstrcmp(name, "wait4switch")) {
		lua_pushnumber(L, job_desc->wait4switch);
	} else if (!xstrcmp(name, "work_dir")) {
		lua_pushstring(L, job_desc->work_dir);
	} else if (!xstrcmp(name, "wckey")) {
		lua_pushstring(L, job_desc->wckey);
	} else {
		lua_pushnil(L);
	}

	return 1;
}

/* Get fields in the job request record on job submit or modify */
static int _get_job_req_field_name(lua_State *L)
{
	const job_desc_msg_t *job_desc = lua_touserdata(L, 1);
	const char *name = luaL_checkstring(L, 2);

	return _get_job_req_field(job_desc, name);
}

/* Get fields in an existing slurmctld job_descriptor record */
static int _get_job_req_field_index(lua_State *L)
{
	const char *name;
	job_desc_msg_t *job_desc;

	name = luaL_checkstring(L, 2);
	lua_getmetatable(L, -2);
	lua_getfield(L, -1, "_job_desc");
	job_desc = lua_touserdata(L, -1);

	return _get_job_req_field(job_desc, name);
}

/* Set fields in the job request structure on job submit or modify */
static int _set_job_req_field(lua_State *L)
{
	const char *name, *value_str;
	job_desc_msg_t *job_desc;

	name = luaL_checkstring(L, 2);
	lua_getmetatable(L, -3);
	lua_getfield(L, -1, "_job_desc");
	job_desc = lua_touserdata(L, -1);
	if (job_desc == NULL) {
		error("%s: job_desc is NULL", __func__);
	} else if (!xstrcmp(name, "account")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->account);
		if (strlen(value_str))
			job_desc->account = xstrdup(value_str);
	} else if (!xstrcmp(name, "acctg_freq")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->acctg_freq);
		if (strlen(value_str))
			job_desc->acctg_freq = xstrdup(value_str);
	} else if (!xstrcmp(name, "admin_comment")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->admin_comment);
		if (strlen(value_str))
			job_desc->admin_comment = xstrdup(value_str);
	} else if (!xstrcmp(name, "array_inx")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->array_inx);
		if (strlen(value_str))
			job_desc->array_inx = xstrdup(value_str);
	} else if (!xstrcmp(name, "batch_features")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->batch_features);
		if (strlen(value_str))
			job_desc->batch_features = xstrdup(value_str);
	} else if (!xstrcmp(name, "begin_time")) {
		job_desc->begin_time = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "bitflags")) {
		job_desc->bitflags = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "burst_buffer")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->burst_buffer);
		if (strlen(value_str))
			job_desc->burst_buffer = xstrdup(value_str);
	} else if (!xstrcmp(name, "clusters")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->clusters);
		if (strlen(value_str))
			job_desc->clusters = xstrdup(value_str);
	} else if (!xstrcmp(name, "comment")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->comment);
		if (strlen(value_str))
			job_desc->comment = xstrdup(value_str);
	} else if (!xstrcmp(name, "contiguous")) {
		job_desc->contiguous = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "cores_per_socket")) {
		job_desc->cores_per_socket = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "cpu_freq_min")) {
		job_desc->cpu_freq_min = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "cpu_freq_max")) {
		job_desc->cpu_freq_max = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "cpu_freq_gov")) {
		job_desc->cpu_freq_gov = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "cpus_per_task")) {
		job_desc->cpus_per_task = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "cpus_per_tres")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->cpus_per_tres);
		if (strlen(value_str))
			job_desc->cpus_per_tres = xstrdup(value_str);
	} else if (!xstrcmp(name, "dependency")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->dependency);
		if (strlen(value_str))
			job_desc->dependency = xstrdup(value_str);
	} else if (!xstrcmp(name, "delay_boot")) {
		job_desc->delay_boot = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "end_time")) {
		job_desc->end_time = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "extra")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->extra);
		if (strlen(value_str))
			job_desc->extra = xstrdup(value_str);
	} else if (!xstrcmp(name, "exc_nodes")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->exc_nodes);
		if (strlen(value_str))
			job_desc->exc_nodes = xstrdup(value_str);
	} else if (!xstrcmp(name, "features")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->features);
		if (strlen(value_str))
			job_desc->features = xstrdup(value_str);
	} else if (!xstrcmp(name, "gres")) {
		/* "gres" replaced by "tres_per_node" in v18.08 */
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->tres_per_node);
		if (strlen(value_str))
			job_desc->tres_per_node = xstrdup(value_str);
	} else if (!xstrcmp(name, "immediate")) {
		job_desc->immediate = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "licenses")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->licenses);
		if (strlen(value_str))
			job_desc->licenses = xstrdup(value_str);
	} else if (!xstrcmp(name, "max_cpus")) {
		job_desc->max_cpus = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "max_nodes")) {
		job_desc->max_nodes = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "mem_per_tres")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->mem_per_tres);
		if (strlen(value_str))
			job_desc->mem_per_tres = xstrdup(value_str);
	} else if (!xstrcmp(name, "min_cpus")) {
		job_desc->min_cpus = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "min_mem_per_cpu")) {
		job_desc->pn_min_memory = luaL_checknumber(L, 3);
		job_desc->pn_min_memory |= MEM_PER_CPU;
	} else if (!xstrcmp(name, "min_mem_per_node")) {
		job_desc->pn_min_memory = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "min_nodes")) {
		job_desc->min_nodes = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "name")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->name);
		if (strlen(value_str))
			job_desc->name = xstrdup(value_str);
	} else if (!xstrcmp(name, "nice")) {
		job_desc->nice = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "ntasks_per_node")) {
		job_desc->ntasks_per_node = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "ntasks_per_socket")) {
		job_desc->ntasks_per_socket = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "num_tasks")) {
		job_desc->num_tasks = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "partition")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->partition);
		if (strlen(value_str))
			job_desc->partition = xstrdup(value_str);
	} else if (!xstrcmp(name, "power_flags")) {
		job_desc->power_flags = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "pn_min_cpus")) {
		job_desc->pn_min_cpus = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "pn_min_memory")) {
		/*
		 * FIXME: Remove this in the future, lua can't handle 64bit
		 * numbers!!!.  Use min_mem_per_node|cpu instead.
		 */
		job_desc->pn_min_memory = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "pn_min_tmp_disk")) {
		job_desc->pn_min_tmp_disk = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "priority")) {
		job_desc->priority = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "qos")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->qos);
		if (strlen(value_str))
			job_desc->qos = xstrdup(value_str);
	} else if (!xstrcmp(name, "reboot")) {
		job_desc->reboot = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "req_nodes")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->req_nodes);
		if (strlen(value_str))
			job_desc->req_nodes = xstrdup(value_str);
	} else if (!xstrcmp(name, "req_switch")) {
		job_desc->req_switch = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "requeue")) {
		job_desc->requeue = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "reservation")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->reservation);
		if (strlen(value_str))
			job_desc->reservation = xstrdup(value_str);
	} else if (!xstrcmp(name, "script")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->script);
		if (strlen(value_str))
			job_desc->script = xstrdup(value_str);
	} else if (!xstrcmp(name, "shared")) {
		job_desc->shared = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "site_factor")) {
		job_desc->site_factor = luaL_checknumber(L, 3);
		if (job_desc->site_factor != NO_VAL)
			job_desc->site_factor =
				NICE_OFFSET + job_desc->site_factor;
	} else if (!xstrcmp(name, "sockets_per_node")) {
		job_desc->sockets_per_node = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "std_err")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->std_err);
		if (strlen(value_str))
			job_desc->std_err = xstrdup(value_str);
	} else if (!xstrcmp(name, "std_in")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->std_in);
		if (strlen(value_str))
			job_desc->std_in = xstrdup(value_str);
	} else if (!xstrcmp(name, "std_out")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->std_out);
		if (strlen(value_str))
			job_desc->std_out = xstrdup(value_str);
	} else if (!xstrcmp(name, "threads_per_core")) {
		job_desc->threads_per_core = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "time_limit")) {
		job_desc->time_limit = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "time_min")) {
		job_desc->time_min = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "tres_bind")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->tres_bind);
		if (strlen(value_str))
			job_desc->tres_bind = xstrdup(value_str);
	} else if (!xstrcmp(name, "tres_freq")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->tres_freq);
		if (strlen(value_str))
			job_desc->tres_freq = xstrdup(value_str);
	} else if (!xstrcmp(name, "tres_per_job")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->tres_per_job);
		if (strlen(value_str))
			job_desc->tres_per_job = xstrdup(value_str);
	} else if (!xstrcmp(name, "tres_per_node")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->tres_per_node);
		if (strlen(value_str))
			job_desc->tres_per_node = xstrdup(value_str);
	} else if (!xstrcmp(name, "tres_per_socket")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->tres_per_socket);
		if (strlen(value_str))
			job_desc->tres_per_socket = xstrdup(value_str);
	} else if (!xstrcmp(name, "tres_per_task")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->tres_per_task);
		if (strlen(value_str))
			job_desc->tres_per_task = xstrdup(value_str);
	} else if (!xstrcmp(name, "wait4switch")) {
		job_desc->wait4switch = luaL_checknumber(L, 3);
	} else if (!xstrcmp(name, "wckey")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->wckey);
		if (strlen(value_str))
			job_desc->wckey = xstrdup(value_str);
	} else if (!xstrcmp(name, "work_dir")) {
		value_str = luaL_checkstring(L, 3);
		xfree(job_desc->work_dir);
		if (strlen(value_str))
			job_desc->work_dir = xstrdup(value_str);
	} else {
		error("_set_job_field: unrecognized field: %s", name);
	}

	return 0;
}

static void _push_job_desc(job_desc_msg_t *job_desc)
{
	lua_newtable(L);

	lua_newtable(L);
	lua_pushcfunction(L, _get_job_req_field_index);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, _set_job_req_field);
	lua_setfield(L, -2, "__newindex");
	/* Store the job descriptor in the metatable, so the index
	 * function knows which struct it's getting data for.
	 */
	lua_pushlightuserdata(L, job_desc);
	lua_setfield(L, -2, "_job_desc");
	lua_setmetatable(L, -2);
}

static void _push_job_rec(job_record_t *job_ptr)
{
	lua_newtable(L);

	lua_newtable(L);
	lua_pushcfunction(L, _job_rec_field_index);
	lua_setfield(L, -2, "__index");
	/* Store the job_ptr in the metatable, so the index
	 * function knows which struct it's getting data for.
	 */
	lua_pushlightuserdata(L, job_ptr);
	lua_setfield(L, -2, "_job_rec_ptr");
	lua_setmetatable(L, -2);
}

/* Get fields in an existing slurmctld partition record
 *
 * This is an incomplete list of partition record fields. Add more as needed
 * and send patches to slurm-dev@schedmd.com
 */
static int _part_rec_field(const part_record_t *part_ptr, const char *name)
{
	if (part_ptr == NULL) {
		error("_get_part_field: part_ptr is NULL");
		lua_pushnil(L);
	} else if (!xstrcmp(name, "allow_accounts")) {
		lua_pushstring(L, part_ptr->allow_accounts);
	} else if (!xstrcmp(name, "allow_alloc_nodes")) {
		lua_pushstring(L, part_ptr->allow_alloc_nodes);
	} else if (!xstrcmp(name, "allow_groups")) {
		lua_pushstring(L, part_ptr->allow_groups);
	} else if (!xstrcmp(name, "allow_qos")) {
		lua_pushstring(L, part_ptr->allow_qos);
	} else if (!xstrcmp(name, "alternate")) {
		lua_pushstring(L, part_ptr->alternate);
	} else if (!xstrcmp(name, "billing_weights_str")) {
		lua_pushstring(L, part_ptr->billing_weights_str);
	} else if (!xstrcmp(name, "default_time")) {
		lua_pushnumber(L, part_ptr->default_time);
	} else if (!xstrcmp(name, "def_mem_per_cpu") &&
		   (part_ptr->def_mem_per_cpu != NO_VAL64) &&
		   (part_ptr->def_mem_per_cpu & MEM_PER_CPU)) {
		lua_pushnumber(L, part_ptr->def_mem_per_cpu & (~MEM_PER_CPU));
	} else if (!xstrcmp(name, "def_mem_per_node") &&
		   (part_ptr->def_mem_per_cpu != NO_VAL64) &&
		   !(part_ptr->def_mem_per_cpu & MEM_PER_CPU)) {
		lua_pushnumber(L, part_ptr->def_mem_per_cpu);
	} else if (!xstrcmp(name, "deny_accounts")) {
		lua_pushstring(L, part_ptr->deny_accounts);
	} else if (!xstrcmp(name, "deny_qos")) {
		lua_pushstring(L, part_ptr->deny_qos);
	} else if (!xstrcmp(name, "flag_default")) {
		int is_default = 0;
		if (part_ptr->flags & PART_FLAG_DEFAULT)
			is_default = 1;
		lua_pushnumber(L, is_default);
	} else if (!xstrcmp(name, "flags")) {
		lua_pushnumber(L, part_ptr->flags);
	} else if (!xstrcmp(name, "max_cpus_per_node")) {
		lua_pushnumber(L, part_ptr->max_cpus_per_node);
	} else if (!xstrcmp(name, "max_mem_per_cpu") &&
		   (part_ptr->max_mem_per_cpu != NO_VAL64) &&
		   (part_ptr->max_mem_per_cpu & MEM_PER_CPU)) {
		lua_pushnumber(L, part_ptr->max_mem_per_cpu & (~MEM_PER_CPU));
	} else if (!xstrcmp(name, "max_mem_per_node") &&
		   (part_ptr->max_mem_per_cpu != NO_VAL64) &&
		   !(part_ptr->max_mem_per_cpu & MEM_PER_CPU)) {
		lua_pushnumber(L, part_ptr->max_mem_per_cpu);
	} else if (!xstrcmp(name, "max_nodes")) {
		lua_pushnumber(L, part_ptr->max_nodes);
	} else if (!xstrcmp(name, "max_nodes_orig")) {
		lua_pushnumber(L, part_ptr->max_nodes_orig);
	} else if (!xstrcmp(name, "max_share")) {
		lua_pushnumber(L, part_ptr->max_share);
	} else if (!xstrcmp(name, "max_time")) {
		lua_pushnumber(L, part_ptr->max_time);
	} else if (!xstrcmp(name, "min_nodes")) {
		lua_pushnumber(L, part_ptr->min_nodes);
	} else if (!xstrcmp(name, "min_nodes_orig")) {
		lua_pushnumber(L, part_ptr->min_nodes_orig);
	} else if (!xstrcmp(name, "name")) {
		lua_pushstring(L, part_ptr->name);
	} else if (!xstrcmp(name, "nodes")) {
		lua_pushstring(L, part_ptr->nodes);
	} else if (!xstrcmp(name, "priority_job_factor")) {
		lua_pushnumber(L, part_ptr->priority_job_factor);
	} else if (!xstrcmp(name, "priority_tier")) {
		lua_pushnumber(L, part_ptr->priority_tier);
	} else if (!xstrcmp(name, "qos")) {
		lua_pushstring(L, part_ptr->qos_char);
	} else if (!xstrcmp(name, "state_up")) {
		lua_pushnumber(L, part_ptr->state_up);
	} else {
		lua_pushnil(L);
	}

	return 1;
}

static int _get_part_rec_field (lua_State *L)
{
	const part_record_t *part_ptr = lua_touserdata(L, 1);
	const char *name = luaL_checkstring(L, 2);

	return _part_rec_field(part_ptr, name);
}

static int _part_rec_field_index(lua_State *L)
{
	const char *name = luaL_checkstring(L, 2);
	part_record_t *part_ptr;

	lua_getmetatable(L, -2);
	lua_getfield(L, -1, "_part_rec_ptr");
	part_ptr = lua_touserdata(L, -1);

	return _part_rec_field(part_ptr, name);
}

static bool _user_can_use_part(uint32_t user_id, uint32_t submit_uid,
			       part_record_t *part_ptr)
{
	int i;

	if (user_id == 0) {
		if (part_ptr->flags & PART_FLAG_NO_ROOT)
			return false;
		return true;
	}

	if ((part_ptr->flags & PART_FLAG_ROOT_ONLY) && (submit_uid != 0))
		return false;

	if (part_ptr->allow_uids == NULL)
		return true;	/* No user ID filters */

	for (i=0; part_ptr->allow_uids[i]; i++) {
		if (user_id == part_ptr->allow_uids[i])
			return true;
	}
	return false;
}

static void _push_partition_list(uint32_t user_id, uint32_t submit_uid)
{
	ListIterator part_iterator;
	part_record_t *part_ptr;

	lua_newtable(L);
	part_iterator = list_iterator_create(part_list);
	while ((part_ptr = list_next(part_iterator))) {
		if (!_user_can_use_part(user_id, submit_uid, part_ptr))
			continue;

		/* Create an empty table, with a metatable that looks up the
		 * data for the partition.
		 */
		lua_newtable(L);

		lua_newtable(L);
		lua_pushcfunction(L, _part_rec_field_index);
		lua_setfield(L, -2, "__index");
		/* Store the part_record in the metatable, so the index
		 * function knows which job it's getting data for.
		 */
		lua_pushlightuserdata(L, part_ptr);
		lua_setfield(L, -2, "_part_rec_ptr");
		lua_setmetatable(L, -2);

		lua_setfield(L, -2, part_ptr->name);
	}
	list_iterator_destroy(part_iterator);
}


static int _log_lua_user_msg (lua_State *L)
{
	const char *msg = lua_tostring(L, -1);
	char *tmp = NULL;

	if (user_msg) {
		xstrfmtcat(tmp, "%s\n%s", user_msg, msg);
		xfree(user_msg);
		user_msg = tmp;
		tmp = NULL;
	} else {
		user_msg = xstrdup(msg);
	}

	return (0);
}

static const struct luaL_Reg slurm_functions [] = {
	{ "user_msg",   _log_lua_user_msg },
	{ NULL,         NULL        }
};


static void _register_local_output_functions(lua_State *L)
{
	char *unpack_str;
	char tmp_string[100];

#if LUA_VERSION_NUM == 501
	unpack_str = "unpack";
#else
	unpack_str = "table.unpack";
#endif

	slurm_lua_table_register(L, NULL, slurm_functions);
	snprintf(tmp_string, sizeof(tmp_string),
		 "slurm.user_msg (string.format(%s({...})))",
		 unpack_str);
	luaL_loadstring(L, tmp_string);
	lua_setfield(L, -2, "log_user");

	/* Must be always done after we register the slurm_functions */
	lua_setglobal(L, "slurm");

	last_lua_jobs_update = 0;
	_update_jobs_global(L);
	last_lua_resv_update = 0;
	_update_resvs_global(L);
}

static void _register_lua_slurm_struct_functions(lua_State *st)
{
	lua_pushcfunction(st, _get_job_env_field_name);
	lua_setglobal(st, "_get_job_env_field_name");
	lua_pushcfunction(st, _get_job_req_field_name);
	lua_setglobal(st, "_get_job_req_field_name");
	lua_pushcfunction(st, _set_job_env_field);
	lua_setglobal(st, "_set_job_env_field");
	lua_pushcfunction(st, _set_job_req_field);
	lua_setglobal(st, "_set_job_req_field");
	lua_pushcfunction(st, _get_part_rec_field);
	lua_setglobal(st, "_get_part_rec_field");
}

static void _loadscript_extra(lua_State *st)
{
	/* local setup */
	_register_local_output_functions(st);
	_register_lua_slurm_struct_functions(st);
}

/*
 *  NOTE: The init callback should never be called multiple times,
 *   let alone called from multiple threads. Therefore, locking
 *   is unnecessary here.
 */
int init(void)
{
	int rc = SLURM_SUCCESS;

	if ((rc = slurm_lua_init()) != SLURM_SUCCESS)
		return rc;

	return slurm_lua_loadscript(&L, "job_submit/lua",
				    lua_script_path, req_fxns,
				    &lua_script_last_loaded,
				    _loadscript_extra);
}

int fini(void)
{
	if (L) {
		debug3("%s: Unloading Lua script", __func__);
		lua_close(L);
		L = NULL;
		lua_script_last_loaded = 0;
	}

	slurm_lua_fini();

	return SLURM_SUCCESS;
}


/* Lua script hook called for "submit job" event. */
extern int job_submit(job_desc_msg_t *job_desc, uint32_t submit_uid,
		      char **err_msg)
{
	int rc;
	slurm_mutex_lock (&lua_lock);

	rc = slurm_lua_loadscript(&L, "job_submit/lua",
				  lua_script_path, req_fxns,
				  &lua_script_last_loaded, _loadscript_extra);

	if (rc != SLURM_SUCCESS)
		goto out;

	/*
	 *  All lua script functions should have been verified during
	 *   initialization:
	 */
	lua_getglobal(L, "slurm_job_submit");
	if (lua_isnil(L, -1))
		goto out;

	_update_jobs_global(L);
	_update_resvs_global(L);

	_push_job_desc(job_desc);
	_push_partition_list(job_desc->user_id, submit_uid);
	lua_pushnumber(L, submit_uid);
	slurm_lua_stack_dump(
		"job_submit/lua", "job_submit, before lua_pcall", L);
	if (lua_pcall(L, 3, 1, 0) != 0) {
		error("%s/lua: %s: %s",
		      __func__, lua_script_path, lua_tostring(L, -1));
	} else {
		if (lua_isnumber(L, -1)) {
			rc = lua_tonumber(L, -1);
		} else {
			info("%s/lua: %s: non-numeric return code",
			     __func__, lua_script_path);
			rc = SLURM_SUCCESS;
		}
		lua_pop(L, 1);
	}
	slurm_lua_stack_dump(
		"job_submit/lua", "job_submit, after lua_pcall", L);
	if (user_msg) {
		*err_msg = user_msg;
		user_msg = NULL;
	}

out:	slurm_mutex_unlock (&lua_lock);
	return rc;
}

/* Lua script hook called for "modify job" event. */
extern int job_modify(job_desc_msg_t *job_desc, job_record_t *job_ptr,
		      uint32_t submit_uid)
{
	int rc;
	slurm_mutex_lock (&lua_lock);

	rc = slurm_lua_loadscript(&L, "job_submit/lua",
				  lua_script_path, req_fxns,
				  &lua_script_last_loaded, _loadscript_extra);

	if (rc == SLURM_ERROR)
		goto out;

	/*
	 *  All lua script functions should have been verified during
	 *   initialization:
	 */
	lua_getglobal(L, "slurm_job_modify");
	if (lua_isnil(L, -1))
		goto out;

	_update_jobs_global(L);
	_update_resvs_global(L);

	_push_job_desc(job_desc);
	_push_job_rec(job_ptr);
	_push_partition_list(job_ptr->user_id, submit_uid);
	lua_pushnumber(L, submit_uid);
	slurm_lua_stack_dump(
		"job_submit/lua", "job_modify, before lua_pcall", L);
	if (lua_pcall(L, 4, 1, 0) != 0) {
		error("%s/lua: %s: %s",
		      __func__, lua_script_path, lua_tostring(L, -1));
	} else {
		if (lua_isnumber(L, -1)) {
			rc = lua_tonumber(L, -1);
		} else {
			info("%s/lua: %s: non-numeric return code",
			     __func__, lua_script_path);
			rc = SLURM_SUCCESS;
		}
		lua_pop(L, 1);
	}
	slurm_lua_stack_dump(
		"job_submit/lua", "job_modify, after lua_pcall", L);
	if (user_msg) {
		error("Use of log.user() in job_modify is not supported. "
		      "Message discarded: (\"%s\")", user_msg);
		xfree(user_msg);
	}

out:	slurm_mutex_unlock (&lua_lock);
	return rc;
}
