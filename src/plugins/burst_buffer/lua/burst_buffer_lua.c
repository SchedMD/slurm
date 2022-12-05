/*****************************************************************************\
 *  burst_buffer_lua.c - Plugin for managing burst buffers with lua
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
 *  Written by Marshall Garey <marshall@schedmd.com>
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

#include "config.h"

#define _GNU_SOURCE

#include <ctype.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdlib.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/assoc_mgr.h"
#include "src/common/data.h"
#include "src/common/fd.h"
#include "src/common/run_command.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/interfaces/serializer.h"
#include "src/lua/slurm_lua.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmscriptd.h"
#include "src/slurmctld/trigger_mgr.h"
#include "src/plugins/burst_buffer/common/burst_buffer_common.h"

/* Script directive */
#define DEFAULT_DIRECTIVE_STR "BB_LUA"
/* Hold job if pre_run fails more times than MAX_RETRY_CNT */
#define MAX_RETRY_CNT 2
/*
 * Limit the number of burst buffers APIs allowed to run in parallel so that we
 * don't exceed process or system resource limits (such as number of processes
 * or max open files) when we run scripts through slurmscriptd. We limit this
 * per "stage" (stage in, pre run, stage out, teardown) so that if we hit the
 * maximum in stage in (for example) we won't block all jobs from completing.
 * We also do this so that if 1000+ jobs complete or get cancelled all at
 * once they won't all run teardown at the same time.
 */
#define MAX_BURST_BUFFERS_PER_STAGE 128

/*
 * These variables are required by the burst buffer plugin interface.  If they
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
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "burst_buffer" for Slurm burst_buffer) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will only
 * load a burst_buffer plugin if the plugin_type string has a prefix of
 * "burst_buffer/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "burst_buffer lua plugin";
const char plugin_type[]        = "burst_buffer/lua";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/*
 * Most state information is in a common structure so that we can more
 * easily use common functions from multiple burst buffer plugins.
 */
static bb_state_t bb_state;

static char *directive_str;
static int directive_len = 0;

static char *lua_script_path;
static const char *req_fxns[] = {
	"slurm_bb_job_process",
	"slurm_bb_pools",
	"slurm_bb_job_teardown",
	"slurm_bb_setup",
	"slurm_bb_data_in",
	"slurm_bb_real_size",
	"slurm_bb_paths",
	"slurm_bb_pre_run",
	"slurm_bb_post_run",
	"slurm_bb_data_out",
	"slurm_bb_get_status",
	NULL
};

/* Description of each pool entry */
typedef struct bb_pools {
	char *name;
	uint64_t granularity;
	uint64_t quantity;
	uint64_t free;
} bb_pools_t;

typedef struct {
	int i;
	int num_pools;
	bb_pools_t *pools;
} data_pools_arg_t;

typedef struct {
	uint32_t group_id;
	bool hurry;
	uint32_t job_id;
	uint32_t user_id;
	char *job_script;
} teardown_args_t;

typedef struct {
	uint64_t bb_size;
	uint32_t gid;
	uint32_t job_id;
	char *job_script;
	char *pool;
	uint32_t uid;
} stage_in_args_t;

typedef struct {
	uint32_t gid;
	uint32_t job_id;
	char *job_script;
	uint32_t timeout;
	uint32_t uid;
} pre_run_args_t;

typedef struct {
	uint32_t gid;
	uint32_t job_id;
	char *job_script;
	uint32_t uid;
} stage_out_args_t;

typedef struct {
	uint32_t argc;
	char **argv;
} status_args_t;

typedef struct {
	uint32_t argc;
	char **argv;
	bool get_job_ptr;
	bool have_job_lock;
	uint32_t job_id;
	job_record_t *job_ptr;
	char *lua_func;
	char **resp_msg;
	uint32_t timeout;
	bool *track_script_signal;
	bool with_scriptd;
} run_lua_args_t;

static int lua_thread_cnt = 0;
pthread_mutex_t lua_thread_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Count of burst buffer API calls in each stage.
 * These variables are protected by stage_cnt_mutex.
 */
#define DEF_STAGE_THROTTLE \
	static pthread_mutex_t stage_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;\
	static pthread_cond_t stage_cnt_cond = PTHREAD_COND_INITIALIZER;\
	static int stage_cnt = 0;

/*
 * stage throttle doesn't guarantee the order each thread will start. For
 * stage_in we need to run burst_buffer.lua in job priority order so that
 * highest priority jobs can start as soon as possible. With this we only queue
 * up to MAX_BURST_BUFFERS_PER_STAGE _start_stage_in threads at once, so we
 * don't use stage throttle for stage_in.
 * This variable is protected by bb_state.bb_mutex.
 */
static int stage_in_cnt = 0;

/* Function prototypes */
static bb_job_t *_get_bb_job(job_record_t *job_ptr);
static void _queue_teardown(uint32_t job_id, uint32_t user_id, bool hurry,
			    uint32_t group_id);

static int _get_lua_thread_cnt(void)
{
	int cnt;

	slurm_mutex_lock(&lua_thread_mutex);
	cnt = lua_thread_cnt;
	slurm_mutex_unlock(&lua_thread_mutex);

	return cnt;
}

static void _incr_lua_thread_cnt(void)
{
	slurm_mutex_lock(&lua_thread_mutex);
	lua_thread_cnt++;
	slurm_mutex_unlock(&lua_thread_mutex);
}

static void _decr_lua_thread_cnt(void)
{
	slurm_mutex_lock(&lua_thread_mutex);
	lua_thread_cnt--;
	slurm_mutex_unlock(&lua_thread_mutex);
}

static void _stage_throttle_start(pthread_mutex_t *mutex, pthread_cond_t *cond,
				  int *cnt)
{
	slurm_mutex_lock(mutex);
	while (1) {
		if (*cnt < MAX_BURST_BUFFERS_PER_STAGE) {
			*cnt = *cnt + 1;
			break;
		}
		slurm_cond_wait(cond, mutex);
	}
	slurm_mutex_unlock(mutex);
}

static void _stage_throttle_fini(pthread_mutex_t *mutex, pthread_cond_t *cond,
				 int *cnt)
{
	slurm_mutex_lock(mutex);
	*cnt = *cnt - 1;
	slurm_cond_broadcast(cond);
	slurm_mutex_unlock(mutex);
}

static int _job_info_to_string(lua_State *L)
{
	job_info_t *job_info;
	char *msg;

	/* Pushes the metatable of the table onto the stack */
	if (!lua_getmetatable(L, -1)) {
		error("job_info_to_string requires one argument - job_info table");
		lua_pushinteger(L, SLURM_ERROR);
		lua_pushstring(L, "job_info_to_string requires one argument - job_info table");
		return 2;
	}

	/*
	 * Pushes metatable["_job_info_ptr"] onto the stack, which is just a
	 * pointer to job_info.
	 */
	lua_getfield(L, -1, "_job_info_ptr");
	/* Now we can get the pointer to job_info from the top of the stack */
	job_info = lua_touserdata(L, -1);

	msg = slurm_sprint_job_info(job_info, 0);
	lua_pushinteger(L, SLURM_SUCCESS);
	lua_pushstring(L, msg);

	return 2;
}

static const struct luaL_Reg slurm_functions [] = {
	{ "job_info_to_string", _job_info_to_string },
	{ NULL, NULL }
};

static void _loadscript_extra(lua_State *st)
{
        /* local setup */
	slurm_lua_table_register(st, NULL, slurm_functions);

	/* Must be always done after we register the slurm_functions */
	lua_setglobal(st, "slurm");
}

static int _lua_job_info_field(lua_State *L, const job_info_t *job_info,
			       const char *name)
{
	/*
	 * Be careful with 64-bit numbers.
	 * Lua prior to 5.3 stored all numbers as bit floating point numbers,
	 * which can cause loss of precision.
	 * Lua 5.3 onward can store 64-bit signed integers, but not unsigned
	 * integers, but Lua will also convert between its integer and floating
	 * point data types for certain operations - see section 3.4.3
	 * "Coercions and Conversions" of the Lua manual.
	 */
	if (!job_info) {
		error("_job_info_field: job_info is NULL");
		lua_pushnil(L);
	} else if (!xstrcmp(name, "account")) {
		lua_pushstring(L, job_info->account);
	} else if (!xstrcmp(name, "accrue_time")) {
		lua_pushinteger(L, job_info->accrue_time);
	} else if (!xstrcmp(name, "admin_comment")) {
		lua_pushstring(L, job_info->admin_comment);
	} else if (!xstrcmp(name, "alloc_node")) {
		lua_pushstring(L, job_info->alloc_node);
	} else if (!xstrcmp(name, "alloc_sid")) {
		lua_pushinteger(L, job_info->alloc_sid);
	} else if (!xstrcmp(name, "array_job_id")) {
		lua_pushinteger(L, job_info->array_job_id);
	} else if (!xstrcmp(name, "array_task_id")) {
		lua_pushinteger(L, job_info->array_task_id);
	} else if (!xstrcmp(name, "array_max_tasks")) {
		lua_pushinteger(L, job_info->array_max_tasks);
	} else if (!xstrcmp(name, "array_task_str")) {
		lua_pushstring(L, job_info->array_task_str);
	} else if (!xstrcmp(name, "assoc_id")) {
		lua_pushinteger(L, job_info->assoc_id);
	} else if (!xstrcmp(name, "batch_features")) {
		lua_pushstring(L, job_info->batch_features);
	} else if (!xstrcmp(name, "batch_flag")) {
		lua_pushinteger(L, job_info->batch_flag);
	} else if (!xstrcmp(name, "batch_host")) {
		lua_pushstring(L, job_info->batch_host);
	/* Ignore bitflags */
	} else if (!xstrcmp(name, "boards_per_node")) {
		lua_pushinteger(L, job_info->boards_per_node);
	} else if (!xstrcmp(name, "burst_buffer")) {
		lua_pushstring(L, job_info->burst_buffer);
	} else if (!xstrcmp(name, "burst_buffer_state")) {
		lua_pushstring(L, job_info->burst_buffer_state);
	} else if (!xstrcmp(name, "cluster")) {
		lua_pushstring(L, job_info->cluster);
	} else if (!xstrcmp(name, "cluster_features")) {
		lua_pushstring(L, job_info->cluster_features);
	} else if (!xstrcmp(name, "command")) {
		lua_pushstring(L, job_info->command);
	} else if (!xstrcmp(name, "comment")) {
		lua_pushstring(L, job_info->comment);
	} else if (!xstrcmp(name, "container")) {
		lua_pushstring(L, job_info->container);
	} else if (!xstrcmp(name, "container_id")) {
		lua_pushstring(L, job_info->container_id);
	} else if (!xstrcmp(name, "contiguous")) {
		lua_pushinteger(L, job_info->contiguous);
	} else if (!xstrcmp(name, "core_spec")) {
		lua_pushinteger(L, job_info->core_spec);
	} else if (!xstrcmp(name, "cores_per_socket")) {
		lua_pushinteger(L, job_info->cores_per_socket);
	} else if (!xstrcmp(name, "billable_tres")) {
		lua_pushnumber(L, job_info->billable_tres);
	} else if (!xstrcmp(name, "cpus_per_task")) {
		lua_pushinteger(L, job_info->cpus_per_task);
	} else if (!xstrcmp(name, "cpu_freq_min")) {
		lua_pushinteger(L, job_info->cpu_freq_min);
	} else if (!xstrcmp(name, "cpu_freq_max")) {
		lua_pushinteger(L, job_info->cpu_freq_max);
	} else if (!xstrcmp(name, "cpu_freq_gov")) {
		lua_pushinteger(L, job_info->cpu_freq_gov);
	} else if (!xstrcmp(name, "cpus_per_tres")) {
		lua_pushstring(L, job_info->cpus_per_tres);
	} else if (!xstrcmp(name, "cronspec")) {
		lua_pushstring(L, job_info->cronspec);
	} else if (!xstrcmp(name, "deadline")) {
		lua_pushinteger(L, job_info->deadline);
	} else if (!xstrcmp(name, "delay_boot")) {
		lua_pushinteger(L, job_info->delay_boot);
	} else if (!xstrcmp(name, "dependency")) {
		lua_pushstring(L, job_info->dependency);
	} else if (!xstrcmp(name, "derived_ec")) {
		lua_pushinteger(L, job_info->derived_ec);
	} else if (!xstrcmp(name, "eligible_time")) {
		lua_pushinteger(L, job_info->eligible_time);
	} else if (!xstrcmp(name, "end_time")) {
		lua_pushinteger(L, job_info->end_time);
	} else if (!xstrcmp(name, "exc_nodes")) {
		lua_pushstring(L, job_info->exc_nodes);
	/* Ignore exc_node_inx */
	} else if (!xstrcmp(name, "exit_code")) {
		lua_pushinteger(L, job_info->exit_code);
	} else if (!xstrcmp(name, "features")) {
		lua_pushstring(L, job_info->features);
	} else if (!xstrcmp(name, "fed_origin_str")) {
		lua_pushstring(L, job_info->fed_origin_str);
	/* Ignore fed_siblings_active */
	} else if (!xstrcmp(name, "fed_siblings_active_str")) {
		lua_pushstring(L, job_info->fed_siblings_active_str);
	/* Ignore fed_siblings_viable */
	} else if (!xstrcmp(name, "fed_siblings_viable_str")) {
		lua_pushstring(L, job_info->fed_siblings_viable_str);
	} else if (!xstrcmp(name, "gres_detail_cnt")) {
		lua_pushinteger(L, job_info->gres_detail_cnt);
	} else if (!xstrcmp(name, "gres_detail_str")) {
		if (!job_info->gres_detail_cnt)
			lua_pushnil(L);
		else {
			/*
			 * Add a table: key=index i+1, value=gres_detail_str[i]
			 * (index=i+1 because Lua is one-indexed)
			 */
			lua_newtable(L);
			for (int i = 0; i < job_info->gres_detail_cnt; i++) {
				lua_pushinteger(L, i+1);
				lua_pushstring(L, job_info->gres_detail_str[i]);
				/*
				 * Adds this key-value pair to the table which
				 * is 3 from the top of the stack
				 */
				lua_settable(L, -3);
			}
		}
	} else if (!xstrcmp(name, "gres_total")) {
		lua_pushstring(L, job_info->gres_total);
	} else if (!xstrcmp(name, "group_id")) {
		lua_pushinteger(L, job_info->group_id);
	} else if (!xstrcmp(name, "het_job_id")) {
		lua_pushinteger(L, job_info->het_job_id);
	} else if (!xstrcmp(name, "het_job_id_set")) {
		lua_pushstring(L, job_info->het_job_id_set);
	} else if (!xstrcmp(name, "het_job_offset")) {
		lua_pushinteger(L, job_info->het_job_offset);
	} else if (!xstrcmp(name, "job_id")) {
		lua_pushinteger(L, job_info->job_id);
	/* Ignore job_resrcs */
	} else if (!xstrcmp(name, "job_state")) {
		lua_pushinteger(L, job_info->job_state);
	} else if (!xstrcmp(name, "last_sched_eval")) {
		lua_pushinteger(L, job_info->last_sched_eval);
	} else if (!xstrcmp(name, "licenses")) {
		lua_pushstring(L, job_info->licenses);
	} else if (!xstrcmp(name, "mail_type")) {
		lua_pushinteger(L, job_info->mail_type);
	} else if (!xstrcmp(name, "mail_user")) {
		lua_pushstring(L, job_info->mail_user);
	} else if (!xstrcmp(name, "max_cpus")) {
		lua_pushinteger(L, job_info->max_cpus);
	} else if (!xstrcmp(name, "max_nodes")) {
		lua_pushinteger(L, job_info->max_nodes);
	} else if (!xstrcmp(name, "mcs_label")) {
		lua_pushstring(L, job_info->mcs_label);
	} else if (!xstrcmp(name, "mem_per_tres")) {
		lua_pushstring(L, job_info->mem_per_tres);
	} else if (!xstrcmp(name, "min_mem_per_node")) {
		if ((job_info->pn_min_memory != NO_VAL64) &&
		    !(job_info->pn_min_memory & MEM_PER_CPU))
			lua_pushinteger(L, job_info->pn_min_memory);
		else
			lua_pushnil(L);
	} else if (!xstrcmp(name, "min_mem_per_cpu")) {
		if ((job_info->pn_min_memory != NO_VAL64) &&
		    (job_info->pn_min_memory & MEM_PER_CPU))
			lua_pushinteger(L,
				       job_info->pn_min_memory & ~MEM_PER_CPU);
		else
			lua_pushnil(L);
	} else if (!xstrcmp(name, "name")) {
		lua_pushstring(L, job_info->name);
	} else if (!xstrcmp(name, "network")) {
		lua_pushstring(L, job_info->network);
	} else if (!xstrcmp(name, "nodes")) {
		lua_pushstring(L, job_info->nodes);
	} else if (!xstrcmp(name, "nice")) {
		lua_pushinteger(L, job_info->nice);
	/* Ignore node_inx */
	} else if (!xstrcmp(name, "ntasks_per_core")) {
		lua_pushinteger(L, job_info->ntasks_per_core);
	} else if (!xstrcmp(name, "ntasks_per_tres")) {
		lua_pushinteger(L, job_info->ntasks_per_tres);
	} else if (!xstrcmp(name, "ntasks_per_node")) {
		lua_pushinteger(L, job_info->ntasks_per_node);
	} else if (!xstrcmp(name, "ntasks_per_socket")) {
		lua_pushinteger(L, job_info->ntasks_per_socket);
	} else if (!xstrcmp(name, "ntasks_per_board")) {
		lua_pushinteger(L, job_info->ntasks_per_board);
	} else if (!xstrcmp(name, "num_cpus")) {
		lua_pushinteger(L, job_info->num_cpus);
	} else if (!xstrcmp(name, "num_nodes")) {
		lua_pushinteger(L, job_info->num_nodes);
	} else if (!xstrcmp(name, "num_tasks")) {
		lua_pushinteger(L, job_info->num_tasks);
	} else if (!xstrcmp(name, "partition")) {
		lua_pushstring(L, job_info->partition);
	} else if (!xstrcmp(name, "prefer")) {
		lua_pushstring(L, job_info->prefer);
	/* Ignore pn_min_memory - use min_mem_per_node|cpu instead */
	} else if (!xstrcmp(name, "pn_min_cpus")) {
		lua_pushinteger(L, job_info->pn_min_cpus);
	} else if (!xstrcmp(name, "pn_min_tmp_disk")) {
		lua_pushinteger(L, job_info->pn_min_tmp_disk);
	} else if (!xstrcmp(name, "power_flags")) {
		lua_pushinteger(L, job_info->power_flags);
	} else if (!xstrcmp(name, "preempt_time")) {
		lua_pushinteger(L, job_info->preempt_time);
	} else if (!xstrcmp(name, "preemptable_time")) {
		lua_pushinteger(L, job_info->preemptable_time);
	} else if (!xstrcmp(name, "pre_sus_time")) {
		lua_pushinteger(L, job_info->pre_sus_time);
	} else if (!xstrcmp(name, "priority")) {
		lua_pushinteger(L, job_info->priority);
	} else if (!xstrcmp(name, "profile")) {
		lua_pushinteger(L, job_info->profile);
	} else if (!xstrcmp(name, "qos")) {
		lua_pushstring(L, job_info->qos);
	} else if (!xstrcmp(name, "reboot")) {
		lua_pushinteger(L, job_info->reboot);
	} else if (!xstrcmp(name, "req_nodes")) {
		lua_pushstring(L, job_info->req_nodes);
	/* Ignore req_node_inx */
	} else if (!xstrcmp(name, "req_switch")) {
		lua_pushinteger(L, job_info->req_switch);
	} else if (!xstrcmp(name, "requeue")) {
		lua_pushinteger(L, job_info->requeue);
	} else if (!xstrcmp(name, "resize_time")) {
		lua_pushinteger(L, job_info->resize_time);
	} else if (!xstrcmp(name, "restart_cnt")) {
		lua_pushinteger(L, job_info->restart_cnt);
	} else if (!xstrcmp(name, "resv_name")) {
		lua_pushstring(L, job_info->resv_name);
	} else if (!xstrcmp(name, "sched_nodes")) {
		lua_pushstring(L, job_info->sched_nodes);
	/* Ignore select_jobinfo */
	} else if (!xstrcmp(name, "selinux_context")) {
		lua_pushstring(L, job_info->selinux_context);
	} else if (!xstrcmp(name, "shared")) {
		lua_pushinteger(L, job_info->shared);
	} else if (!xstrcmp(name, "show_flags")) {
		lua_pushinteger(L, job_info->show_flags);
	} else if (!xstrcmp(name, "site_factor")) {
		lua_pushinteger(L, job_info->site_factor);
	} else if (!xstrcmp(name, "sockets_per_board")) {
		lua_pushinteger(L, job_info->sockets_per_board);
	} else if (!xstrcmp(name, "sockets_per_node")) {
		lua_pushinteger(L, job_info->sockets_per_node);
	} else if (!xstrcmp(name, "start_time")) {
		lua_pushinteger(L, job_info->start_time);
	} else if (!xstrcmp(name, "start_protocol_ver")) {
		lua_pushinteger(L, job_info->start_protocol_ver);
	} else if (!xstrcmp(name, "state_desc")) {
		lua_pushstring(L, job_info->state_desc);
	} else if (!xstrcmp(name, "state_reason")) {
		lua_pushinteger(L, job_info->state_reason);
	} else if (!xstrcmp(name, "std_err")) {
		lua_pushstring(L, job_info->std_err);
	} else if (!xstrcmp(name, "std_in")) {
		lua_pushstring(L, job_info->std_in);
	} else if (!xstrcmp(name, "std_out")) {
		lua_pushstring(L, job_info->std_out);
	} else if (!xstrcmp(name, "submit_time")) {
		lua_pushinteger(L, job_info->submit_time);
	} else if (!xstrcmp(name, "suspend_time")) {
		lua_pushinteger(L, job_info->suspend_time);
	} else if (!xstrcmp(name, "system_comment")) {
		lua_pushstring(L, job_info->system_comment);
	} else if (!xstrcmp(name, "time_limit")) {
		lua_pushinteger(L, job_info->time_limit);
	} else if (!xstrcmp(name, "time_min")) {
		lua_pushinteger(L, job_info->time_min);
	} else if (!xstrcmp(name, "threads_per_core")) {
		lua_pushinteger(L, job_info->threads_per_core);
	} else if (!xstrcmp(name, "tres_bind")) {
		lua_pushstring(L, job_info->tres_bind);
	} else if (!xstrcmp(name, "tres_freq")) {
		lua_pushstring(L, job_info->tres_freq);
	} else if (!xstrcmp(name, "tres_per_job")) {
		lua_pushstring(L, job_info->tres_per_job);
	} else if (!xstrcmp(name, "tres_per_node")) {
		lua_pushstring(L, job_info->tres_per_node);
	} else if (!xstrcmp(name, "tres_per_socket")) {
		lua_pushstring(L, job_info->tres_per_socket);
	} else if (!xstrcmp(name, "tres_per_task")) {
		lua_pushstring(L, job_info->tres_per_task);
	} else if (!xstrcmp(name, "tres_req_str")) {
		lua_pushstring(L, job_info->tres_req_str);
	} else if (!xstrcmp(name, "tres_alloc_str")) {
		lua_pushstring(L, job_info->tres_alloc_str);
	} else if (!xstrcmp(name, "user_id")) {
		lua_pushinteger(L, job_info->user_id);
	} else if (!xstrcmp(name, "user_name")) {
		lua_pushstring(L, job_info->user_name);
	} else if (!xstrcmp(name, "wait4switch")) {
		lua_pushinteger(L, job_info->wait4switch);
	} else if (!xstrcmp(name, "wckey")) {
		lua_pushstring(L, job_info->wckey);
	} else if (!xstrcmp(name, "work_dir")) {
		lua_pushstring(L, job_info->work_dir);
	} else {
		lua_pushnil(L);
	}

	return 1;
}

/*
 * Arguments are passed to us on the stack of L:
 * (1) The table (second from the top of the stack)
 * (2) The key (top of the stack)
 */
static int _job_info_field_index(lua_State *L)
{
	const char *name = luaL_checkstring(L, 2);
	job_info_t *job_info;

	/* Pushes the metatable of the table onto the stack */
	lua_getmetatable(L, -2);
	/*
	 * Pushes metatable["_job_info_ptr"] onto the stack, which is just a
	 * pointer to job_info.
	 */
	lua_getfield(L, -1, "_job_info_ptr");
	/* Now we can get the pointer to job_info from the top of the stack */
	job_info = lua_touserdata(L, -1);

	return _lua_job_info_field(L, job_info, name);
}

/*
 * This function results in a single metatable on the stack:
 *
 * <metatable> = {
 *   __index = _job_info_field_index
 *   _job_info_ptr = job_info
 * }
 *
 * The metatable defines the behavior of special operations on the table.
 * The __index operation is called when indexing job_info in lua.
 * When burst_buffer.lua does this:
 *   job_info["some_value"]
 * Then the function _job_info_field_index is called with the table and key
 * as the arguments:
 *   (1) This job_info table, (2) "some_value"
 *
 * See the lua manual section 2.4 Metatables and Metamethods for the various
 * table operations (which are identified by keys in the metatable).
 *
 * Negative indices reference the top of the stack:
 *   stack[-1] is the top of the stack, stack[-2] is the second from the top of
 *   the stack, etc.
 */
static void _push_job_info(job_info_t *job_info, lua_State *L)
{
	/*
	 * Push a table onto the stack
	 * Stack after this call:
	 * -1  table1
	 */
	lua_newtable(L);

	/*
	 * Push a table the stack.
	 * Stack after this call:
	 * -1  table2
	 * -2  table1
	 */
	lua_newtable(L);
	/*
	 * Push a c function to the stack.
	 * Stack after this call:
	 * -1  function = _push_job_info_field_index
	 * -2  table2
	 * -3  table1
	 */
	lua_pushcfunction(L, _job_info_field_index);
	/*
	 * - Pop _job_info_field_index
	 * - table2["__index"] = _job_info_field_index
	 * Stack after this call:
	 * -1  table2 = {
	 *     __index = _job_info_field_index
	 *   }
	 * -2  table1
	 */
	lua_setfield(L, -2, "__index");
	/*
	 * The next two calls store the job_info in the metatable, so the index
	 * function knows which struct it's getting data for.
	 * lightuserdata represents a C pointer (a void*).
	 */
	/*
	 * Pushes lightuserdata onto the stack.
	 * Stack after this call:
	 * -1 userdata = pointer to job_info
	 * -2  table2 = {
	 *     __index = _job_info_field_index
	 *   }
	 * -3  table1
	 */
	lua_pushlightuserdata(L, job_info);
	/*
	 * - Pop userdata (pointer to job_info) from the stack
	 * - table2["_job_info_ptr"] = userdata
	 * Stack after this call:
	 * Assigns second from top value on stack to the value on the top
	 * of the stack and pops the top value from the stack
	 * -1  table2 = {
	 *     __index = _job_info_field_index
	 *     _job_info_ptr = job_info
	 *   }
	 * -2  table1
	 */
	lua_setfield(L, -2, "_job_info_ptr");
	/*
	 * Pops a value from the stack and sets it as the new metatable for
	 * the value at the given index (second from the top value on the stack)
	 * - Pop table2 from the stack
	 * - table1 metatable = table2
	 * Stack after this call:
	 * -1 table1 metatable = {
	 *      __index = _job_info_field_index
	 *     _job_info_ptr = job_info
	 *    }
	 */
	lua_setmetatable(L, -2);
}

static int _handle_lua_return_code(lua_State *L, const char *lua_func)
{
	/* Return code is always the bottom of the stack. */
	if (!lua_isnumber(L, 1)) {
		error("%s: %s returned a non-numeric return code, returning error",
		      __func__, lua_func);
		return SLURM_ERROR;
	} else {
		return lua_tonumber(L, 1);
	}
}

static void _print_lua_rc_msg(int rc, const char *lua_func, uint32_t job_id,
			      char *resp_msg)
{
	/*
	 * Some burst buffer APIs don't run for a specific job. But if they
	 * do run for a specific job, log the job ID.
	 */
	if (job_id)
		log_flag(BURST_BUF, "%s for JobId=%u returned, status=%d, response=%s",
			 lua_func, job_id, rc, resp_msg);
	else
		log_flag(BURST_BUF, "%s returned, status=%d, response=%s",
			 lua_func, rc, resp_msg);
}

static int _handle_lua_return(lua_State *L, const char *lua_func,
			      uint32_t job_id, char **ret_str)
{
	int rc = SLURM_SUCCESS;
	int num_stack_elems = lua_gettop(L);

	if (!num_stack_elems) {
		log_flag(BURST_BUF, "%s finished and didn't return anything",
			 lua_func);
		return rc; /* No results, return success. */
	}

	/* Bottom of the stack should be the return code. */
	rc = _handle_lua_return_code(L, lua_func);

	if (num_stack_elems > 1) {
		/*
		 * Multiple results. Right now we only consider up to 2 results,
		 * and the second should be a string.
		 */
		xassert(ret_str);

		if (lua_isstring(L, 2)) {
			xfree(*ret_str);
			/*
			 * Valgrind thinks that we leak this lua_tostring() by
			 * calling xstrdup and not free'ing the string on the
			 * lua stack, but lua will garbage collect it after
			 * we pop it off the stack.
			 */
			*ret_str = xstrdup(lua_tostring(L, 2));
		} else {
			/* Don't know how to handle non-strings here. */
			error("%s: Cannot handle non-string as second return value for lua function %s.",
			      __func__, lua_func);
			rc = SLURM_ERROR;
		}
	}

	if (ret_str)
		_print_lua_rc_msg(rc, lua_func, job_id, *ret_str);
	else
		_print_lua_rc_msg(rc, lua_func, job_id, NULL);

	/* Pop everything from the stack. */
	lua_pop(L, num_stack_elems);

	return rc;
}

static int _start_lua_script(char *func, uint32_t job_id, uint32_t argc,
			     char **argv, job_info_msg_t *job_info,
			     char **resp_msg)
{
	/*
	 * We don't make lua_State L or lua_script_last_loaded static.
	 * If they were static, then only 1 thread could use them at a time.
	 * This would be problematic for performance since these
	 * calls can possibly last a long time. By not making them static it
	 * means we can let these calls run in parallel, but it also means
	 * we don't preserve the previous script. Therefore, we have to
	 * reload the script every time even if the script hasn't changed.
	 * Also, if there is ever a problem loading the script then we can't
	 * fall back to the old script.
	 */
	lua_State *L = NULL;
	time_t lua_script_last_loaded = (time_t) 0;
	int rc, i;

	rc = slurm_lua_loadscript(&L, "burst_buffer/lua",
				  lua_script_path, req_fxns,
				  &lua_script_last_loaded, _loadscript_extra);

	if (rc != SLURM_SUCCESS)
		return rc;

	/*
	 * All lua script functions should have been verified during
	 * initialization:
	 */
	lua_getglobal(L, func);
	if (lua_isnil(L, -1)) {
		error("%s: Couldn't find function %s",
		      __func__, func);
		lua_close(L);
		return SLURM_ERROR;
	}

	for (i = 0; i < argc; i++)
		lua_pushstring(L, argv[i]);
	if (job_info) {
		job_info_t *info = &job_info->job_array[0];
		_push_job_info(info, L);
		argc++;
	}

	slurm_lua_stack_dump("burst_buffer/lua", "before lua_pcall", L);

	/* Run the lua command and tell the calling thread when it's done. */
	if ((rc = lua_pcall(L, argc, LUA_MULTRET, 0)) != 0) {
		error("%s: %s", lua_script_path, lua_tostring(L, -1));
		rc = SLURM_ERROR;
		lua_pop(L, lua_gettop(L));
	} else {
		slurm_lua_stack_dump("burst_buffer/lua", "after lua_pcall, before returns have been popped", L);
		rc = _handle_lua_return(L, func, job_id, resp_msg);
	}
	slurm_lua_stack_dump("burst_buffer/lua", "after lua_pcall, after returns have been popped", L);
	lua_close(L);

	return rc;
}

/*
 * Call a function in burst_buffer.lua.
 */
static int _run_lua_script(run_lua_args_t *args)
{
	int rc;
	int job_buf_size = 0;
	char *job_buf = NULL;
	List job_ids = NULL;
	job_record_t *job_ptr;
	slurmctld_lock_t job_read_lock = {
		.conf = READ_LOCK, .job = READ_LOCK,
	};

	if (args->get_job_ptr) {
		if (!args->have_job_lock)
			lock_slurmctld(job_read_lock);

		if (args->job_ptr)
			job_ptr = args->job_ptr;
		else {
			job_ptr = find_job_record(args->job_id);
			if (!job_ptr) {
				error("Unable to find job record for JobId=%u, cannot run %s",
				      args->job_id, args->lua_func);
				if (args->resp_msg)
					*args->resp_msg =
						xstrdup_printf("Unable to find job record for JobId=%u, cannot run %s",
							       args->job_id,
							       args->lua_func);
				rc = SLURM_ERROR;
				if (!args->have_job_lock)
					unlock_slurmctld(job_read_lock);
				return rc;
			}
		}
		job_ids = list_create(NULL);
		list_append(job_ids, &job_ptr->job_id);
		pack_spec_jobs(&job_buf, &job_buf_size, job_ids, SHOW_DETAIL,
			       slurm_conf.slurm_user_id, NO_VAL,
			       SLURM_PROTOCOL_VERSION);

		if (!args->have_job_lock)
			unlock_slurmctld(job_read_lock);
	}

	_incr_lua_thread_cnt();
	if (args->with_scriptd) {
		rc = slurmscriptd_run_bb_lua(args->job_id,
					     args->lua_func,
					     args->argc,
					     args->argv,
					     args->timeout,
					     job_buf,
					     job_buf_size,
					     args->resp_msg,
					     args->track_script_signal);
	} else {
		job_info_msg_t *job_info = NULL;

		if (job_buf) {
			buf_t *info_buf;
			slurm_msg_t *info_msg = xmalloc(sizeof *info_msg);

			slurm_msg_t_init(info_msg);
			info_msg->protocol_version = SLURM_PROTOCOL_VERSION;
			info_msg->msg_type = RESPONSE_JOB_INFO;
			info_buf = create_buf(job_buf, job_buf_size);
			unpack_msg(info_msg, info_buf);
			job_info = info_msg->data;
			info_msg->data = NULL;

			/*
			 * create_buf() does not duplicate the data, just
			 * points to it.  So just NULL it out here. It will get
			 * free'd later.
			 */
			info_buf->head = NULL;
			FREE_NULL_BUFFER(info_buf);
			slurm_free_msg(info_msg);
		}
		rc = _start_lua_script(args->lua_func, args->job_id, args->argc,
				       args->argv, job_info, args->resp_msg);
		if (job_buf) {
			slurm_free_job_info_msg(job_info);
		}
	}
	_decr_lua_thread_cnt();

	FREE_NULL_LIST(job_ids);
	xfree(job_buf);

	return rc;
}

/*
 * Write current burst buffer state to a file.
 */
static void _save_bb_state(void)
{
	static time_t last_save_time = 0;
	static int high_buffer_size = 16 * 1024;
	time_t save_time = time(NULL);
	bb_alloc_t *bb_alloc;
	uint32_t rec_count = 0;
	buf_t *buffer;
	char *old_file = NULL, *new_file = NULL, *reg_file = NULL;
	int i, count_offset, offset;
	uint16_t protocol_version = SLURM_PROTOCOL_VERSION;

	if ((bb_state.last_update_time <= last_save_time) &&
	    !bb_state.term_flag)
		return;

	buffer = init_buf(high_buffer_size);
	pack16(protocol_version, buffer);
	count_offset = get_buf_offset(buffer);
	pack32(rec_count, buffer);
	/* Each allocated burst buffer is in bb_state.bb_ahash */
	if (bb_state.bb_ahash) {
		slurm_mutex_lock(&bb_state.bb_mutex);
		for (i = 0; i < BB_HASH_SIZE; i++) {
			bb_alloc = bb_state.bb_ahash[i];
			while (bb_alloc) {
				packstr(bb_alloc->account, buffer);
				pack_time(bb_alloc->create_time, buffer);
				pack32(bb_alloc->id, buffer);
				packstr(bb_alloc->name, buffer);
				packstr(bb_alloc->partition, buffer);
				packstr(bb_alloc->pool, buffer);
				packstr(bb_alloc->qos, buffer);
				pack32(bb_alloc->user_id, buffer);
				pack32(bb_alloc->group_id, buffer);
				pack64(bb_alloc->size,	buffer);
				rec_count++;
				bb_alloc = bb_alloc->next;
			}
		}
		save_time = time(NULL);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		offset = get_buf_offset(buffer);
		set_buf_offset(buffer, count_offset);
		pack32(rec_count, buffer);
		set_buf_offset(buffer, offset);
	}

	xstrfmtcat(old_file, "%s/%s", slurm_conf.state_save_location,
	           "burst_buffer_lua_state.old");
	xstrfmtcat(reg_file, "%s/%s", slurm_conf.state_save_location,
	           "burst_buffer_lua_state");
	xstrfmtcat(new_file, "%s/%s", slurm_conf.state_save_location,
	           "burst_buffer_lua_state.new");

	bb_write_state_file(old_file, reg_file, new_file, "burst_buffer_lua",
			    buffer, high_buffer_size, save_time,
			    &last_save_time);

	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	FREE_NULL_BUFFER(buffer);
}

static void _recover_bb_state(void)
{
	char *state_file = NULL, *data = NULL;
	int data_allocated, data_read = 0;
	uint16_t protocol_version = NO_VAL16;
	uint32_t data_size = 0, rec_count = 0, name_len = 0;
	uint32_t id = 0, user_id = 0, group_id = 0;
	uint64_t size = 0;
	int i, state_fd;
	char *account = NULL, *name = NULL;
	char *partition = NULL, *pool = NULL, *qos = NULL;
	char *end_ptr = NULL;
	time_t create_time = 0;
	bb_alloc_t *bb_alloc;
	buf_t *buffer;

	state_fd = bb_open_state_file("burst_buffer_lua_state", &state_file);
	if (state_fd < 0) {
		info("No burst buffer state file (%s) to recover",
		     state_file);
		xfree(state_file);
		return;
	}
	data_allocated = BUF_SIZE;
	data = xmalloc(data_allocated);
	while (1) {
		data_read = read(state_fd, &data[data_size], BUF_SIZE);
		if (data_read < 0) {
			if  (errno == EINTR)
				continue;
			else {
				error("Read error on %s: %m", state_file);
				break;
			}
		} else if (data_read == 0)     /* eof */
			break;
		data_size      += data_read;
		data_allocated += data_read;
		xrealloc(data, data_allocated);
	}
	close(state_fd);
	xfree(state_file);

	buffer = create_buf(data, data_size);
	safe_unpack16(&protocol_version, buffer);
	if (protocol_version == NO_VAL16) {
		if (!ignore_state_errors)
			fatal("Can not recover burst_buffer/lua state, data version incompatible, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
		error("**********************************************************************");
		error("Can not recover burst_buffer/lua state, data version incompatible");
		error("**********************************************************************");
		return;
	}

	safe_unpack32(&rec_count, buffer);
	for (i = 0; i < rec_count; i++) {
		if (protocol_version >= SLURM_23_02_PROTOCOL_VERSION) {
			safe_unpackstr_xmalloc(&account,   &name_len, buffer);
			safe_unpack_time(&create_time, buffer);
			safe_unpack32(&id, buffer);
			safe_unpackstr_xmalloc(&name,      &name_len, buffer);
			safe_unpackstr_xmalloc(&partition, &name_len, buffer);
			safe_unpackstr_xmalloc(&pool,      &name_len, buffer);
			safe_unpackstr_xmalloc(&qos,       &name_len, buffer);
			safe_unpack32(&user_id, buffer);
			safe_unpack32(&group_id, buffer);
			safe_unpack64(&size, buffer);
		} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			safe_unpackstr_xmalloc(&account,   &name_len, buffer);
			safe_unpack_time(&create_time, buffer);
			safe_unpack32(&id, buffer);
			safe_unpackstr_xmalloc(&name,      &name_len, buffer);
			safe_unpackstr_xmalloc(&partition, &name_len, buffer);
			safe_unpackstr_xmalloc(&pool,      &name_len, buffer);
			safe_unpackstr_xmalloc(&qos,       &name_len, buffer);
			safe_unpack32(&user_id, buffer);
			safe_unpack64(&size, buffer);
		}

		slurm_mutex_lock(&bb_state.bb_mutex);
		bb_alloc = bb_alloc_name_rec(&bb_state, name, user_id);
		bb_alloc->group_id = group_id;
		bb_alloc->id = id;
		if (name && (name[0] >='0') && (name[0] <='9')) {
			bb_alloc->job_id = strtol(name, &end_ptr, 10);
			bb_alloc->array_job_id = bb_alloc->job_id;
			bb_alloc->array_task_id = NO_VAL;
		}
		bb_alloc->seen_time = time(NULL);
		bb_alloc->size = size;
		log_flag(BURST_BUF, "Recovered burst buffer %s from user %u",
			 bb_alloc->name, bb_alloc->user_id);
		bb_alloc->account = account;
		account = NULL;
		bb_alloc->create_time = create_time;
		bb_alloc->partition = partition;
		partition = NULL;
		bb_alloc->pool = pool;
		pool = NULL;
		bb_alloc->qos = qos;
		qos = NULL;
		slurm_mutex_unlock(&bb_state.bb_mutex);
		xfree(name);
	}

	info("Recovered state of %d burst buffers", rec_count);
	FREE_NULL_BUFFER(buffer);
	return;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete burst buffer data checkpoint file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Incomplete burst buffer data checkpoint file");
	xfree(account);
	xfree(name);
	xfree(partition);
	xfree(qos);
	FREE_NULL_BUFFER(buffer);
	return;
}

/* For a given user/partition/account, set it's assoc_ptr */
static void _set_assoc_mgr_ptrs(bb_alloc_t *bb_alloc)
{
	slurmdb_assoc_rec_t assoc_rec;
	slurmdb_qos_rec_t qos_rec;

	memset(&assoc_rec, 0, sizeof(slurmdb_assoc_rec_t));
	assoc_rec.acct      = bb_alloc->account;
	assoc_rec.partition = bb_alloc->partition;
	assoc_rec.uid       = bb_alloc->user_id;
	if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
				    accounting_enforce,
				    &bb_alloc->assoc_ptr,
				    true) == SLURM_SUCCESS) {
		xfree(bb_alloc->assocs);
		if (bb_alloc->assoc_ptr) {
			bb_alloc->assocs =
				xstrdup_printf(",%u,", bb_alloc->assoc_ptr->id);
		}
	}

	memset(&qos_rec, 0, sizeof(slurmdb_qos_rec_t));
	qos_rec.name = bb_alloc->qos;
	if (assoc_mgr_fill_in_qos(acct_db_conn, &qos_rec, accounting_enforce,
				  &bb_alloc->qos_ptr, true) != SLURM_SUCCESS)
		verbose("Invalid QOS name: %s",
			bb_alloc->qos);

}

static void _apply_limits(void)
{
	bb_alloc_t *bb_alloc;
	/* read locks on assoc */
	assoc_mgr_lock_t assoc_locks =
		{ .assoc = READ_LOCK, .qos = READ_LOCK, .user = READ_LOCK };

	assoc_mgr_lock(&assoc_locks);
	slurm_mutex_lock(&bb_state.bb_mutex);
	for (int i = 0; i < BB_HASH_SIZE; i++) {
		bb_alloc = bb_state.bb_ahash[i];
		while (bb_alloc) {
			info("Recovered buffer Name:%s User:%u Pool:%s Size:%"PRIu64,
			     bb_alloc->name, bb_alloc->user_id,
			     bb_alloc->pool, bb_alloc->size);
			_set_assoc_mgr_ptrs(bb_alloc);
			bb_limit_add(bb_alloc->user_id, bb_alloc->size,
				     bb_alloc->pool, &bb_state, true);
			bb_alloc = bb_alloc->next;
		}
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);
	assoc_mgr_unlock(&assoc_locks);
}

static void _bb_free_pools(bb_pools_t *pools, int num_ent)
{
	for (int i = 0; i < num_ent; i++)
		xfree(pools[i].name);

	xfree(pools);
}

static int _data_get_val_from_key(data_t *data, char *key, data_type_t type,
				  bool required, void *out_val)
{
	int rc = SLURM_SUCCESS;
	data_t *data_tmp = NULL;
	char **val_str;
	int64_t *val_int;

	data_tmp = data_key_get(data, key);
	if (!data_tmp) {
		if (required)
			return SLURM_ERROR;
		return SLURM_SUCCESS; /* Not specified */
	}

	if (data_get_type(data_tmp) != type) {
		error("%s: %s is the wrong data type", __func__, key);
		return SLURM_ERROR;
	}

	switch (type) {
	case DATA_TYPE_STRING:
		val_str = out_val;
		*val_str = xstrdup(data_get_string(data_tmp));
		break;
	case DATA_TYPE_INT_64:
		val_int = out_val;
		*val_int = data_get_int(data_tmp);
		break;
	default:
		rc = SLURM_ERROR;
		break;
	}

	return rc;
}

/*
 * IN data - A dictionary describing a pool.
 * IN/OUT arg - pointer to data_pools_arg_t. This function populates a pool
 *              in arg->pools.
 *
 * RET data_for_each_cmd_t
 */
static data_for_each_cmd_t _foreach_parse_pool(data_t *data, void *arg)
{
	data_pools_arg_t *data_arg = arg;
	data_for_each_cmd_t rc = DATA_FOR_EACH_CONT;
	bb_pools_t *pools = data_arg->pools;
	int i = data_arg->i;

	if (i > data_arg->num_pools) {
		/* This should never happen. */
		error("%s: Got more pools than are in the dict. Cannot parse pools.",
		      __func__);
		rc = DATA_FOR_EACH_FAIL;
		goto next;
	}

	pools[i].free = NO_VAL64;
	pools[i].granularity = NO_VAL64;
	pools[i].quantity = NO_VAL64;

	if (_data_get_val_from_key(data, "id", DATA_TYPE_STRING, true,
				   &pools[i].name) != SLURM_SUCCESS) {
		error("%s: Failure parsing id", __func__);
		rc = DATA_FOR_EACH_FAIL;
		goto next;
	}

	if (_data_get_val_from_key(data, "free", DATA_TYPE_INT_64, false,
				   &pools[i].free) != SLURM_SUCCESS) {
		error("%s: Failure parsing free", __func__);
		rc = DATA_FOR_EACH_FAIL;
		goto next;
	}

	if (_data_get_val_from_key(data, "granularity", DATA_TYPE_INT_64, false,
				   &pools[i].granularity) != SLURM_SUCCESS) {
		error("%s: Failure parsing granularity", __func__);
		rc = DATA_FOR_EACH_FAIL;
		goto next;
	}

	if (_data_get_val_from_key(data, "quantity", DATA_TYPE_INT_64, false,
				   &pools[i].quantity) != SLURM_SUCCESS) {
		error("%s: Failure parsing quantity", __func__);
		rc = DATA_FOR_EACH_FAIL;
		goto next;
	}

next:
	data_arg->i += 1;

	return rc;
}

static bb_pools_t *_bb_get_pools(int *num_pools, uint32_t timeout, int *out_rc)
{
	int rc;
	char *resp_msg = NULL;
	char *lua_func_name = "slurm_bb_pools";
	bb_pools_t *pools = NULL;
	data_pools_arg_t arg;
	data_t *data = NULL;
	data_t *data_tmp = NULL;
	run_lua_args_t run_lua_args = {
		.lua_func = lua_func_name,
		.resp_msg = &resp_msg,
		.timeout = timeout,
	};
	DEF_TIMERS;

	*num_pools = 0;

	/* Call lua function. */
	START_TIMER;
	rc = _run_lua_script(&run_lua_args);
	END_TIMER;
	log_flag(BURST_BUF, "%s ran for %s", lua_func_name, TIME_STR);

	*out_rc = rc;
	if (rc != SLURM_SUCCESS) {
		trigger_burst_buffer();
		return NULL;
	}
	if (!resp_msg) {
		/* This is okay - pools are not required. */
		return NULL;
	}

	rc = serialize_g_string_to_data(&data, resp_msg, strlen(resp_msg),
					MIME_TYPE_JSON);
	if ((rc != SLURM_SUCCESS) || !data) {
		error("%s: Problem parsing \"%s\": %s",
		      __func__, resp_msg, slurm_strerror(rc));
		goto cleanup;
	}

	data_tmp = data_resolve_dict_path(data, "/pools");
	if (!data_tmp || (data_get_type(data_tmp) != DATA_TYPE_LIST)) {
		error("%s: Did not find pools dictionary; problem parsing \"%s\"",
		      __func__, resp_msg);
		goto cleanup;
	}

	*num_pools = (int) data_get_list_length(data_tmp);
	if (*num_pools == 0) {
		error("%s: No pools found, problem parsing \"%s\"",
		      __func__, resp_msg);
		goto cleanup;
	}

	pools = xcalloc(*num_pools, sizeof(*pools));
	arg.num_pools = *num_pools;
	arg.pools = pools;
	arg.i = 0;
	rc = data_list_for_each(data_tmp, _foreach_parse_pool, &arg);
	if (rc <= 0) {
		error("%s: Failed to parse pools: \"%s\"", __func__, resp_msg);
		goto cleanup;
	}

cleanup:
	xfree(resp_msg);
	FREE_NULL_DATA(data);

	return pools;
}

static int _load_pools(uint32_t timeout)
{
	static bool first_run = true;
	bool have_new_pools = false;
	int num_pools = 0, i, j, pools_inx, rc;
	burst_buffer_pool_t *pool_ptr;
	bb_pools_t *pools;
	bitstr_t *pools_bitmap;

	/* Load the pools information from burst_buffer.lua. */
	pools = _bb_get_pools(&num_pools, timeout, &rc);
	if (rc != SLURM_SUCCESS) {
		error("Get pools returned error %d, cannot use pools unless get pools returns success",
		      rc);
		return SLURM_ERROR;
	}
	if (!pools) {
		/* Pools are not required. */
		return SLURM_SUCCESS;
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	pools_bitmap = bit_alloc(bb_state.bb_config.pool_cnt + num_pools);

	/* Put found pools into bb_state.bb_config.pool_ptr. */
	for (i = 0; i < num_pools; i++) {
		bool found_pool = false;
		pool_ptr = bb_state.bb_config.pool_ptr;
		for (j = 0; j < bb_state.bb_config.pool_cnt; j++, pool_ptr++) {
			if (!xstrcmp(pool_ptr->name, pools[i].name)) {
				found_pool = true;
				break;
			}
		}
		if (!found_pool) {
			have_new_pools = true;
			/* This is a new pool. Add it to bb_state. */
			if (!first_run)
				info("Newly reported pool %s", pools[i].name);
			bb_state.bb_config.pool_ptr =
				xrealloc(bb_state.bb_config.pool_ptr,
					 sizeof(burst_buffer_pool_t) *
					 (bb_state.bb_config.pool_cnt + 1));
			pool_ptr = bb_state.bb_config.pool_ptr +
				bb_state.bb_config.pool_cnt;
			pool_ptr->name = xstrdup(pools[i].name);
			bb_state.bb_config.pool_cnt++;
		}

		pools_inx = pool_ptr - bb_state.bb_config.pool_ptr;
		bit_set(pools_bitmap, pools_inx);

		if (!pools[i].granularity ||
		    (pools[i].granularity == NO_VAL64)) {
			if (first_run || !found_pool)
				log_flag(BURST_BUF, "Granularity cannot be zero. Setting granularity to 1 for pool %s",
					 pool_ptr->name);
			pools[i].granularity = 1;
		}
		if (pools[i].quantity == NO_VAL64) {
			if (first_run || !found_pool)
				log_flag(BURST_BUF, "Quantity unset for pool %s, setting to zero",
					 pool_ptr->name);
			pools[i].quantity = 0;
		}
		pool_ptr->total_space = pools[i].quantity *
			pools[i].granularity;
		pool_ptr->granularity = pools[i].granularity;

		/*
		 * Set unfree space. We use pool_ptr->used_space to track
		 * usage of pools within Slurm and this plugin also always
		 * updates pool_ptr->unfree_space at the same time. But we
		 * have unfree_space as a way for the burst buffer API to say
		 * that something external to Slurm is using space, or as a
		 * way to not allow some space to be used.
		 */
		if (pools[i].free != NO_VAL64) {
			if (pools[i].quantity >= pools[i].free) {
				pool_ptr->unfree_space =
					pools[i].quantity - pools[i].free;
				pool_ptr->unfree_space *= pools[i].granularity;
			} else {
				error("Underflow on pool=%s: Free space=%"PRIu64" bigger than quantity=%"PRIu64", setting free space equal to quantity",
				      pools[i].name, pools[i].free,
				      pools[i].quantity);
				pool_ptr->unfree_space = 0;
			}
		} else if (!found_pool) {
			/*
			 * Free space not specified. This is a new pool since
			 * found_pool==false, so set unfree space to 0. Don't
			 * change unfree space for pools that already exist if
			 * it wasn't specified.
			 */
			pool_ptr->unfree_space = 0;
		}
	}

	pool_ptr = bb_state.bb_config.pool_ptr;
	for (j = 0; j < bb_state.bb_config.pool_cnt; j++, pool_ptr++) {
		if (bit_test(pools_bitmap, j) || (pool_ptr->total_space == 0)) {
			if (have_new_pools)
				log_flag(BURST_BUF, "Pool name=%s, granularity=%"PRIu64", total_space=%"PRIu64", used_space=%"PRIu64", unfree_space=%"PRIu64,
					 pool_ptr->name, pool_ptr->granularity,
					 pool_ptr->total_space,
					 pool_ptr->used_space,
					 pool_ptr->unfree_space);
			continue;
		}
		error("Pool %s is no longer reported by the system, setting size to zero",
		      pool_ptr->name);
		pool_ptr->total_space = 0;
		pool_ptr->used_space = 0;
		pool_ptr->unfree_space = 0;
	}
	first_run = false;
	slurm_mutex_unlock(&bb_state.bb_mutex);
	FREE_NULL_BITMAP(pools_bitmap);
	_bb_free_pools(pools, num_pools);

	return SLURM_SUCCESS;
}

static void *_start_stage_out(void *x)
{
	int rc;
	uint32_t timeout, argc;
	char *resp_msg = NULL, *op;
	char **argv;
	bool track_script_signal = false;
	stage_out_args_t *stage_out_args = (stage_out_args_t *) x;
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	job_record_t *job_ptr;
	bb_job_t *bb_job = NULL;
	run_lua_args_t run_lua_args;
	DEF_TIMERS;
	DEF_STAGE_THROTTLE;
	_stage_throttle_start(&stage_cnt_mutex, &stage_cnt_cond, &stage_cnt);

	argc = 4;
	argv = xcalloc(argc + 1, sizeof(char *)); /* NULL-terminated */
	argv[0] = xstrdup_printf("%u", stage_out_args->job_id);
	argv[1] = xstrdup_printf("%s", stage_out_args->job_script);
	argv[2] = xstrdup_printf("%u", stage_out_args->uid);
	argv[3] = xstrdup_printf("%u", stage_out_args->gid);

	timeout = bb_state.bb_config.other_timeout;

	op = "slurm_bb_post_run";

	memset(&run_lua_args, 0, sizeof run_lua_args);
	run_lua_args.argc = argc;
	run_lua_args.argv = argv;
	run_lua_args.get_job_ptr = true;
	run_lua_args.job_id = stage_out_args->job_id;
	run_lua_args.lua_func = op;
	run_lua_args.resp_msg = &resp_msg;
	run_lua_args.timeout = timeout;
	run_lua_args.track_script_signal = &track_script_signal;
	run_lua_args.with_scriptd = true;

	START_TIMER;
	rc = _run_lua_script(&run_lua_args);
	END_TIMER;
	log_flag(BURST_BUF, "%s for JobId=%u ran for %s",
		 op, stage_out_args->job_id, TIME_STR);

	if (track_script_signal) {
		/* Killed by slurmctld, exit now. */
		info("post_run for JobId=%u terminated by slurmctld",
		     stage_out_args->job_id);
		goto fini;
	}

	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(stage_out_args->job_id);
	if (rc != SLURM_SUCCESS) {
		trigger_burst_buffer();
		error("post_run failed for JobId=%u, status: %d, response: %s",
		      stage_out_args->job_id, rc, resp_msg);
		rc = SLURM_ERROR;
	}
	if (!job_ptr) {
		error("unable to find job record for JobId=%u",
		      stage_out_args->job_id);
	} else {
		slurm_mutex_lock(&bb_state.bb_mutex);
		bb_job = _get_bb_job(job_ptr);
		if (bb_job)
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_STAGING_OUT);
		slurm_mutex_unlock(&bb_state.bb_mutex);
	}
	unlock_slurmctld(job_write_lock);

	if (rc == SLURM_SUCCESS) {
		xfree(resp_msg);

		timeout = bb_state.bb_config.stage_out_timeout;
		op = "slurm_bb_data_out";

		memset(&run_lua_args, 0, sizeof run_lua_args);
		run_lua_args.argc = argc;
		run_lua_args.argv = argv;
		run_lua_args.get_job_ptr = true;
		run_lua_args.job_id = stage_out_args->job_id;
		run_lua_args.lua_func = op;
		run_lua_args.resp_msg = &resp_msg;
		run_lua_args.timeout = timeout;
		run_lua_args.track_script_signal = &track_script_signal;
		run_lua_args.with_scriptd = true;

		START_TIMER;
		/* argv is the same as post_run. */
		rc = _run_lua_script(&run_lua_args);
		END_TIMER;
		log_flag(BURST_BUF, "%s for JobId=%u ran for %s",
			 op, stage_out_args->job_id, TIME_STR);

		if (track_script_signal) {
			/* Killed by slurmctld, exit now. */
			info("data_out for JobId=%u terminated by slurmctld",
			     stage_out_args->job_id);
			goto fini;
		}

		if (rc != SLURM_SUCCESS) {
			trigger_burst_buffer();
			error("data_out failed for JobId=%u, status: %d, response: %s",
			      stage_out_args->job_id, rc, resp_msg);
			rc = SLURM_ERROR;
		}
	}

	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(stage_out_args->job_id);
	if (!job_ptr) {
		error("unable to find job record for JobId=%u",
		      stage_out_args->job_id);
	} else {
		slurm_mutex_lock(&bb_state.bb_mutex);
		bb_job = _get_bb_job(job_ptr);
		if (rc != SLURM_SUCCESS) {
			job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
			xfree(job_ptr->state_desc);
			xstrfmtcat(job_ptr->state_desc, "%s: %s: %s",
				   plugin_type, op, resp_msg);
			bb_update_system_comment(job_ptr, op, resp_msg, 1);
			if (bb_state.bb_config.flags &
			    BB_FLAG_TEARDOWN_FAILURE) {
				if (bb_job)
					bb_set_job_bb_state(job_ptr, bb_job,
							    BB_STATE_TEARDOWN);
				_queue_teardown(stage_out_args->job_id,
						stage_out_args->uid,
						false,
						stage_out_args->gid);
			}
		} else {
			job_ptr->job_state &= (~JOB_STAGE_OUT);
			xfree(job_ptr->state_desc);
			last_job_update = time(NULL);
			log_flag(BURST_BUF, "Stage-out/post-run complete for %pJ",
				 job_ptr);
			if (bb_job)
				bb_set_job_bb_state(job_ptr, bb_job,
						    BB_STATE_TEARDOWN);
			_queue_teardown(stage_out_args->job_id,
					stage_out_args->uid, false,
					stage_out_args->gid);
		}
		slurm_mutex_unlock(&bb_state.bb_mutex);
	}
	unlock_slurmctld(job_write_lock);

fini:
	_stage_throttle_fini(&stage_cnt_mutex, &stage_cnt_cond, &stage_cnt);
	xfree(resp_msg);
	xfree(stage_out_args->job_script);
	xfree(stage_out_args);
	xfree_array(argv);

	return NULL;
}

static void _queue_stage_out(job_record_t *job_ptr, bb_job_t *bb_job)
{
	stage_out_args_t *stage_out_args;
	pthread_t tid;

	stage_out_args = xmalloc(sizeof *stage_out_args);
	stage_out_args->job_id = bb_job->job_id;
	stage_out_args->uid = bb_job->user_id;
	stage_out_args->gid = job_ptr->group_id;
	stage_out_args->job_script = bb_handle_job_script(job_ptr, bb_job);

	slurm_thread_create_detached(&tid, _start_stage_out, stage_out_args);
}

static void _pre_queue_stage_out(job_record_t *job_ptr, bb_job_t *bb_job)
{
	bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_POST_RUN);
	job_ptr->job_state |= JOB_STAGE_OUT;
	xfree(job_ptr->state_desc);
	xstrfmtcat(job_ptr->state_desc, "%s: Stage-out in progress",
		   plugin_type);
	_queue_stage_out(job_ptr, bb_job);
}

static void _load_state(bool init_config)
{
	uint32_t timeout;

	timeout = bb_state.bb_config.other_timeout;

	if (_load_pools(timeout) != SLURM_SUCCESS)
		return;

	bb_state.last_load_time = time(NULL);

	if (!init_config)
		return;

	/* Load allocated burst buffers from state files. */
	_recover_bb_state();
	_apply_limits();
	bb_state.last_update_time = time(NULL);

	return;
}

/* Perform periodic background activities */
static void *_bb_agent(void *args)
{
	while (!bb_state.term_flag) {
		bb_sleep(&bb_state, AGENT_INTERVAL);
		if (!bb_state.term_flag) {
			_load_state(false);	/* Has own locking */
		}
		_save_bb_state();	/* Has own locks excluding file write */
	}

	/* Wait for lua threads to finish, then save state once more. */
	while (_get_lua_thread_cnt())
		usleep(100000); /* 100 ms */
	_save_bb_state();

	return NULL;
}

/*
 * Copy a batch job's burst_buffer options into a separate buffer.
 * Merge continued lines into a single line.
 */
static int _xlate_batch(job_desc_msg_t *job_desc)
{
	char *script, *save_ptr = NULL, *tok;
	bool is_cont = false, has_space = false;
	int len, rc = SLURM_SUCCESS;

	xassert(directive_str);

	/*
	 * Any command line --bb options get added to the script
	 */
	if (job_desc->burst_buffer) {
		run_command_add_to_script(&job_desc->script,
					  job_desc->burst_buffer);
		xfree(job_desc->burst_buffer);
	}

	script = xstrdup(job_desc->script);
	tok = strtok_r(script, "\n", &save_ptr);
	while (tok) {
		if (tok[0] != '#')
			break; /* Quit at first non-comment */

		if (xstrncmp(tok + 1, directive_str, directive_len)) {
			/* Skip lines without a burst buffer directive. */
			is_cont = false;
		} else {
			if (is_cont) {
				/*
				 * Continuation of the previous line. Add to
				 * the previous line without the newline and
				 * without repeating the directive.
				 */
				tok += directive_len + 1; /* Add 1 for '#' */
				while (has_space && isspace(tok[0]))
					tok++; /* Skip extra spaces */
			} else if (job_desc->burst_buffer) {
				xstrcat(job_desc->burst_buffer, "\n");
			}

			len = strlen(tok);
			if (tok[len - 1] == '\\') {
				/* Next line is a continuation of this line. */
				has_space = isspace(tok[len - 2]);
				tok[len - 1] = '\0';
				is_cont = true;
			} else {
				is_cont = false;
			}
			xstrcat(job_desc->burst_buffer, tok);
		}
		tok = strtok_r(NULL, "\n", &save_ptr);
	}
	xfree(script);
	if (rc != SLURM_SUCCESS)
		xfree(job_desc->burst_buffer);
	return rc;
}

/*
 * Given a request size and a pool name, return the required buffer size
 * (rounded up by granularity). If no pool name is given then return 0.
 */
static uint64_t _set_granularity(uint64_t orig_size, char *bb_pool)
{
	burst_buffer_pool_t *pool_ptr;
	int i;

	if (!bb_pool)
		return 0;

	for (i = 0, pool_ptr = bb_state.bb_config.pool_ptr;
	     i < bb_state.bb_config.pool_cnt; i++, pool_ptr++) {
		if (!xstrcmp(bb_pool, pool_ptr->name)) {
			if (!pool_ptr->granularity) {
				/*
				 * This should never happen if we initialize
				 * the pools correctly, so if this error happens
				 * it means we initialized the pool wrong.
				 * This avoids a divide by 0 error.
				 */
				error("%s: Invalid granularity of 0 for pool %s. Setting granularity=1.",
				      __func__, pool_ptr->name);
				pool_ptr->granularity = 1;
			}
			return bb_granularity(orig_size, pool_ptr->granularity);
		}
	}
	debug("Could not find pool %s", bb_pool);
	return orig_size;
}

/*
 * IN tok - a line in a burst buffer specification containing "capacity="
 * IN capacity_ptr - pointer to the first character after "capacity=" within tok
 * OUT pool - return a malloc'd string of the pool name, caller is responsible
 *            to free
 * OUT size - return the number specified after "capacity="
 */
static int _parse_capacity(char *tok, char *capacity_ptr, char **pool,
			   uint64_t *size)
{
	char *sub_tok;

	xassert(size);
	xassert(pool);

	*size = bb_get_size_num(capacity_ptr, 1);
	if ((sub_tok = strstr(tok, "pool="))) {
		*pool = xstrdup(sub_tok + 5);
		sub_tok = strchr(*pool, ' ');
		if (sub_tok)
			sub_tok[0] = '\0';
	} else {
		error("%s: Must specify pool with capacity for burst buffer",
		      plugin_type);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/* Perform basic burst_buffer option validation */
static int _parse_bb_opts(job_desc_msg_t *job_desc, uint64_t *bb_size,
			  uid_t submit_uid)
{
	char *bb_script, *save_ptr = NULL;
	char *capacity;
	char *tok;
	int rc = SLURM_SUCCESS;
	bool have_bb = false;

	xassert(bb_size);
	*bb_size = 0;

	if (!directive_str) {
		error("%s: We don't have a directive! Can't parse burst buffer request",
		      __func__);
		return SLURM_ERROR;
	}

	/*
	 * Combine command line options with script, and copy the script to
	 * job_desc->burst_buffer.
	 */
	if (job_desc->script)
		rc = _xlate_batch(job_desc);
	if ((rc != SLURM_SUCCESS) || (!job_desc->burst_buffer))
		return rc;

	/*
	 * Now validate that burst buffer was requested and get the pool and
	 * size if specified.
	 */
	bb_script = xstrdup(job_desc->burst_buffer);
	tok = strtok_r(bb_script, "\n", &save_ptr);
	while (tok) {
		if (tok[0] != '#')
			break; /* Quit at first non-comment */
		tok++; /* Skip '#' */

		if (xstrncmp(tok, directive_str, directive_len)) {
			/* Skip lines without a burst buffer directive. */
			tok = strtok_r(NULL, "\n", &save_ptr);
			continue;
		}

		/*
		 * We only require that the directive is here.
		 * Specifying a pool is optional. Any other needed validation
		 * can be done by the burst_buffer.lua script.
		 */
		have_bb = true;

		tok += directive_len; /* Skip the directive string. */
		while (isspace(tok[0]))
			tok++;
		if ((capacity = strstr(tok, "capacity="))) {
			char *tmp_pool = NULL;
			uint64_t tmp_cnt = 0;

			/*
			 * Lock bb_mutex since we iterate through pools in
			 * bb_state in bb_valid_pool_test() and
			 * _set_granularity().
			 */
			slurm_mutex_lock(&bb_state.bb_mutex);
			if ((rc = _parse_capacity(tok, capacity + 9, &tmp_pool,
						  &tmp_cnt) != SLURM_SUCCESS)) {
				have_bb = false;
			} else if (tmp_cnt == 0) {
				error("%s: Invalid capacity (must be greater than 0) in burst buffer line:%s",
				      plugin_type, tok);
				rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
			} else if (!bb_valid_pool_test(&bb_state, tmp_pool)) {
				rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
			} else
				*bb_size += _set_granularity(tmp_cnt, tmp_pool);
			slurm_mutex_unlock(&bb_state.bb_mutex);

			xfree(tmp_pool);
			if (rc != SLURM_SUCCESS)
				break;
		}
		tok = strtok_r(NULL, "\n", &save_ptr);
	}
	xfree(bb_script);

	if (!have_bb)
		rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;

	return rc;
}

/* Note: bb_mutex is locked on entry */
static bb_job_t *_get_bb_job(job_record_t *job_ptr)
{
	char *bb_specs;
	char *save_ptr = NULL, *sub_tok, *tok;
	bool have_bb = false;
	uint16_t new_bb_state;
	bb_job_t *bb_job;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return NULL;

	if ((bb_job = bb_job_find(&bb_state, job_ptr->job_id)))
		return bb_job;	/* Cached data */

	if (!directive_str) {
		error("%s: We don't have a directive! Can't parse burst buffer request",
		      __func__);
		return NULL;
	}

	bb_job = bb_job_alloc(&bb_state, job_ptr->job_id);
	bb_job->account = xstrdup(job_ptr->account);
	if (job_ptr->part_ptr)
		bb_job->partition = xstrdup(job_ptr->part_ptr->name);
	if (job_ptr->qos_ptr)
		bb_job->qos = xstrdup(job_ptr->qos_ptr->name);
	new_bb_state = job_ptr->burst_buffer_state ?
		bb_state_num(job_ptr->burst_buffer_state) : BB_STATE_PENDING;
	bb_set_job_bb_state(job_ptr, bb_job, new_bb_state);
	bb_job->user_id = job_ptr->user_id;
	bb_specs = xstrdup(job_ptr->burst_buffer);

	tok = strtok_r(bb_specs, "\n", &save_ptr);
	while (tok) {
		/* Skip lines that don't have a burst buffer directive. */
		if ((tok[0] != '#') ||
		    xstrncmp(tok + 1, directive_str, directive_len)) {
			tok = strtok_r(NULL, "\n", &save_ptr);
			continue;
		}

		/*
		 * We only require that the directive is here.
		 * Specifying a pool is optional. Any other needed validation
		 * can be done by the burst_buffer.lua script.
		 */
		have_bb = true;

		/*
		 * Is % symbol replacement required? Only done on lines with
		 * directive_str
		 */
		if (strchr(tok, (int) '%'))
			bb_job->need_symbol_replacement = true;

		tok += directive_len + 1; /* Add 1 for the '#' character. */
		while (isspace(tok[0]))
			tok++;

		if ((sub_tok = strstr(tok, "capacity="))) {
			char *tmp_pool = NULL;
			uint64_t tmp_cnt = 0;

			if (_parse_capacity(tok, sub_tok + 9, &tmp_pool,
					    &tmp_cnt) != SLURM_SUCCESS) {
				have_bb = false;
				xfree(tmp_pool);
				break;
			}
			xfree(bb_job->job_pool);
			bb_job->job_pool = tmp_pool;
			tmp_cnt = _set_granularity(tmp_cnt, bb_job->job_pool);
			bb_job->req_size += tmp_cnt;
			bb_job->total_size += tmp_cnt;
			bb_job->use_job_buf = true;
		}
		tok = strtok_r(NULL, "\n", &save_ptr);
	}
	xfree(bb_specs);

	if (!have_bb) {
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
		xstrfmtcat(job_ptr->state_desc,
			   "%s: Invalid burst buffer spec (%s)",
			   plugin_type, job_ptr->burst_buffer);
		job_ptr->priority = 0;
		info("Invalid burst buffer spec for %pJ (%s)",
		     job_ptr, job_ptr->burst_buffer);
		bb_job_del(&bb_state, job_ptr->job_id);
		return NULL;
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_BURST_BUF)
		bb_job_log(&bb_state, bb_job);
	return bb_job;
}

/* Validate burst buffer configuration */
static void _test_config(void)
{
	/* 24-day max time limit. (2073600 seconds) */
	static uint32_t max_timeout = (60 * 60 * 24 * 24);

	if (bb_state.bb_config.get_sys_state) {
		error("%s: found get_sys_state which is unused in this plugin, unsetting",
		      plugin_type);
		xfree(bb_state.bb_config.get_sys_state);
	}
	if (bb_state.bb_config.get_sys_status) {
		error("%s: found get_sys_status which is unused in this plugin, unsetting",
		      plugin_type);
		xfree(bb_state.bb_config.get_sys_status);
	}
	if (bb_state.bb_config.flags & BB_FLAG_ENABLE_PERSISTENT) {
		error("%s: found flags=EnablePersistent: persistent burst buffers don't exist in this plugin, setting DisablePersistent",
		      plugin_type);
		bb_state.bb_config.flags &= (~BB_FLAG_ENABLE_PERSISTENT);
		bb_state.bb_config.flags |= BB_FLAG_DISABLE_PERSISTENT;
	}
	if (bb_state.bb_config.flags & BB_FLAG_EMULATE_CRAY) {
		error("%s: found flags=EmulateCray which is invalid for this plugin, unsetting",
		      plugin_type);
		bb_state.bb_config.flags &= (~BB_FLAG_EMULATE_CRAY);
	}
	if (bb_state.bb_config.directive_str)
		directive_str = bb_state.bb_config.directive_str;
	else
		directive_str = DEFAULT_DIRECTIVE_STR;
	directive_len = strlen(directive_str);

	if (bb_state.bb_config.default_pool) {
		error("%s: found DefaultPool=%s, but DefaultPool is unused for this plugin, unsetting",
		      plugin_type, bb_state.bb_config.default_pool);
		xfree(bb_state.bb_config.default_pool);
	}

	/*
	 * Burst buffer APIs that would use ValidateTimeout
	 * (slurm_bb_job_process and slurm_bb_paths) are actually called
	 * directly from slurmctld, not through SlurmScriptd. Because of this,
	 * they cannot be killed, so there is no timeout for them. Therefore,
	 * ValidateTimeout doesn't matter in this plugin.
	 */
	if (bb_state.bb_config.validate_timeout &&
	    (bb_state.bb_config.validate_timeout != DEFAULT_VALIDATE_TIMEOUT))
		info("%s: ValidateTimeout is not used in this plugin, ignoring",
		     plugin_type);

	/*
	 * Test time limits. In order to prevent overflow when converting
	 * the time limits in seconds to milliseconds (multiply by 1000),
	 * the maximum value for time limits is 2073600 seconds (24 days).
	 * 2073600 * 1000 is still less than the maximum 32-bit signed integer.
	 */
	if (bb_state.bb_config.other_timeout > max_timeout) {
		error("%s: OtherTimeout=%u exceeds maximum allowed timeout=%u, setting OtherTimeout to maximum",
		      plugin_type, bb_state.bb_config.other_timeout,
		      max_timeout);
		bb_state.bb_config.other_timeout = max_timeout;
	}
	if (bb_state.bb_config.stage_in_timeout > max_timeout) {
		error("%s: StageInTimeout=%u exceeds maximum allowed timeout=%u, setting StageInTimeout to maximum",
		      plugin_type, bb_state.bb_config.stage_in_timeout,
		      max_timeout);
		bb_state.bb_config.stage_in_timeout = max_timeout;
	}
	if (bb_state.bb_config.stage_out_timeout > max_timeout) {
		error("%s: StageOutTimeout=%u exceeds maximum allowed timeout=%u, setting StageOutTimeout to maximum",
		      plugin_type, bb_state.bb_config.stage_out_timeout,
		      max_timeout);
		bb_state.bb_config.stage_out_timeout = max_timeout;
	}
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	int rc;

        if ((rc = slurm_lua_init()) != SLURM_SUCCESS)
                return rc;
	lua_script_path = get_extra_conf_path("burst_buffer.lua");

	if ((rc = data_init())) {
		error("%s: unable to init data structures: %s",
		      __func__, slurm_strerror(rc));
		return rc;
	}

	if ((rc = serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL))) {
		error("%s: unable to load JSON serializer: %s",
		      __func__, slurm_strerror(rc));
		return rc;
	}

	/*
	 * slurmscriptd calls bb_g_init() and then bb_g_run_script(). We only
	 * need to initialize lua to run the script. We don't want
	 * slurmscriptd to read from or write to the state save location, nor
	 * do we need slurmscriptd to load the configuration file.
	 */
	if (!running_in_slurmctld()) {
		return SLURM_SUCCESS;
	}

	slurm_mutex_init(&lua_thread_mutex);
	slurm_mutex_init(&bb_state.bb_mutex);
	slurm_mutex_lock(&bb_state.bb_mutex);
	bb_load_config(&bb_state, (char *)plugin_type); /* Removes "const" */
	_test_config();
	log_flag(BURST_BUF, "");
	bb_alloc_cache(&bb_state);
	slurm_thread_create(&bb_state.bb_thread, _bb_agent, NULL);
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is unloaded. Free all memory.
 */
extern int fini(void)
{
	int thread_cnt, last_thread_cnt = 0;

	/*
	 * Tell bb_agent to stop. It will do one more state save after all
	 * threads have completed.
	 */
	slurm_mutex_lock(&bb_state.term_mutex);
	bb_state.term_flag = true;
	slurm_cond_signal(&bb_state.term_cond);
	slurm_mutex_unlock(&bb_state.term_mutex);

	/* Wait for all running scripts to finish. */
	while ((thread_cnt = _get_lua_thread_cnt())) {
		if ((last_thread_cnt != 0) && (thread_cnt != last_thread_cnt))
			info("Waiting for %d lua script threads", thread_cnt);
		last_thread_cnt = thread_cnt;
		usleep(100000); /* 100 ms */
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "");

	if (bb_state.bb_thread) {
		slurm_mutex_unlock(&bb_state.bb_mutex);
		pthread_join(bb_state.bb_thread, NULL);
		slurm_mutex_lock(&bb_state.bb_mutex);
		bb_state.bb_thread = 0;
	}
	bb_clear_config(&bb_state.bb_config, true);
	bb_clear_cache(&bb_state);
	slurm_mutex_unlock(&bb_state.bb_mutex);

	slurm_mutex_destroy(&lua_thread_mutex);

	slurm_lua_fini();
	xfree(lua_script_path);
	/* Don't call data_fini(), that is taken care of elsewhere. */

	return SLURM_SUCCESS;
}

static void _free_orphan_alloc_rec(void *x)
{
	bb_alloc_t *rec = (bb_alloc_t *)x;

	bb_limit_rem(rec->user_id, rec->size, rec->pool, &bb_state);
	(void) bb_free_alloc_rec(&bb_state, rec);
}

/*
 * This function should only be called from _purge_vestigial_bufs().
 * We need to reset the burst buffer state and restart any threads that may
 * have been running before slurmctld was shutdown, depending on the state
 * that the burst buffer is in.
 */
static void _recover_job_bb(job_record_t *job_ptr, bb_alloc_t *bb_alloc,
			    time_t defer_time, List orphan_rec_list)
{
	bb_job_t *bb_job;
	uint16_t job_bb_state = bb_state_num(job_ptr->burst_buffer_state);

	/*
	 * Call _get_bb_job() to create a cache of the job's burst buffer info,
	 * including the state. Lots of functions will call this so do it now to
	 * create the cache, and we may need to change the burst buffer state.
	 * The job burst buffer state is set in job_ptr and in bb_job.
	 */
	bb_job = _get_bb_job(job_ptr);
	if (!bb_job) {
		/* This shouldn't happen. */
		error("%s: %pJ does not have a burst buffer specification, tearing down vestigial burst buffer.",
		      __func__, job_ptr);
		_queue_teardown(bb_alloc->job_id, bb_alloc->user_id, false,
				bb_alloc->group_id);
		return;
	}

	switch(job_bb_state) {
		/*
		 * First 4 states are specific to persistent burst buffers,
		 * which aren't used in burst_buffer/lua.
		 */
		case BB_STATE_ALLOCATING:
		case BB_STATE_ALLOCATED:
		case BB_STATE_DELETING:
		case BB_STATE_DELETED:
			error("%s: Unexpected burst buffer state %s for %pJ",
			      __func__, job_ptr->burst_buffer_state, job_ptr);
			break;
		/* Pending states for jobs: */
		case BB_STATE_STAGING_IN:
		case BB_STATE_STAGED_IN:
		case BB_STATE_ALLOC_REVOKE:
			/*
			 * We do not know the state of staging,
			 * so teardown the buffer and defer the job
			 * for at least 60 seconds (for the teardown).
			 * Also set the burst buffer state back to PENDING.
			 */
			log_flag(BURST_BUF, "Purging buffer for pending %pJ",
				 job_ptr);
			bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_TEARDOWN);
			_queue_teardown(bb_alloc->job_id,
					bb_alloc->user_id, true,
					bb_alloc->group_id);
			if (job_ptr->details &&
			    (job_ptr->details->begin_time < defer_time)) {
				job_ptr->details->begin_time = defer_time;
			}
			break;
		/* Running states for jobs: */
		case BB_STATE_PRE_RUN:
			/*
			 * slurmctld will call bb_g_job_begin() which will
			 * handle burst buffers in this state.
			 */
			break;
		case BB_STATE_RUNNING:
		case BB_STATE_SUSPEND:
			/* Nothing to do here. */
			break;
		case BB_STATE_POST_RUN:
		case BB_STATE_STAGING_OUT:
		case BB_STATE_STAGED_OUT:
			log_flag(BURST_BUF, "Restarting burst buffer stage out for %pJ",
				 job_ptr);
			/*
			 * _pre_queue_stage_out() sets the burst buffer state
			 * correctly and restarts the needed thread.
			 */
			_pre_queue_stage_out(job_ptr, bb_job);
			break;
		case BB_STATE_TEARDOWN:
		case BB_STATE_TEARDOWN_FAIL:
			log_flag(BURST_BUF, "Restarting burst buffer teardown for %pJ",
				 job_ptr);
			_queue_teardown(bb_alloc->job_id,
					bb_alloc->user_id, false,
					bb_alloc->group_id);
			break;
		case BB_STATE_COMPLETE:
			/*
			 * We shouldn't get here since the bb_alloc record is
			 * removed when the job's bb state is set to
			 * BB_STATE_COMPLETE during teardown.
			 */
			log_flag(BURST_BUF, "Clearing burst buffer for completed job %pJ",
				 job_ptr);
			list_append(orphan_rec_list, bb_alloc);
			break;
		default:
			error("%s: Invalid job burst buffer state %s for %pJ",
			      __func__, job_ptr->burst_buffer_state, job_ptr);
			break;
	}
}

/*
 * Identify and purge any vestigial buffers (i.e. we have a job buffer, but
 * the matching job is either gone or completed OR we have a job buffer and a
 * pending job, but don't know the status of stage-in)
 */
static void _purge_vestigial_bufs(void)
{
	List orphan_rec_list = list_create(_free_orphan_alloc_rec);
	bb_alloc_t *bb_alloc = NULL;
	time_t defer_time = time(NULL) + 60;
	int i;

	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_alloc = bb_state.bb_ahash[i];
		while (bb_alloc) {
			job_record_t *job_ptr = NULL;
			if (bb_alloc->job_id == 0) {
				/* This should not happen */
				error("Burst buffer without a job found, removing buffer.");
				list_append(orphan_rec_list, bb_alloc);
			} else if (!(job_ptr =
				     find_job_record(bb_alloc->job_id))) {
				info("Purging vestigial buffer for JobId=%u",
				     bb_alloc->job_id);
				_queue_teardown(bb_alloc->job_id,
						bb_alloc->user_id, false,
						bb_alloc->group_id);
			} else {
				_recover_job_bb(job_ptr, bb_alloc, defer_time,
						orphan_rec_list);
			}
			bb_alloc = bb_alloc->next;
		}
	}
	FREE_NULL_LIST(orphan_rec_list);
}

static bool _is_directive(char *tok)
{
	xassert(directive_str);
	if ((tok[0] == '#') && !xstrncmp(tok + 1, directive_str, directive_len))
		return true;
	return false;
}

extern char *bb_p_build_het_job_script(char *script, uint32_t het_job_offset)
{
	return bb_common_build_het_job_script(script, het_job_offset,
					      _is_directive);
}

/*
 * Return the total burst buffer size in MB
 */
extern uint64_t bb_p_get_system_size(void)
{
	uint64_t size = 0;

	/*
	 * Add up the space of all the pools.
	 * Don't add bb_state.total_space - it is always zero since we don't
	 * use DefaultPool in this plugin.
	 * Even though the pools in this plugin are really unitless and can
	 * be used for a lot more than just "bytes", we have to convert to MB
	 * to satisfy the burst buffer plugin API.
	 */
	slurm_mutex_lock(&bb_state.bb_mutex);
	for (int i = 0; i < bb_state.bb_config.pool_cnt; i++) {
		size += bb_state.bb_config.pool_ptr[i].total_space;
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);
	size /= (1024 * 1024); /* to MB */

	return size;
}

/*
 * Load the current burst buffer state (e.g. how much space is available now).
 * Run at the beginning of each scheduling cycle in order to recognize external
 * changes to the burst buffer state (e.g. capacity is added, removed, fails,
 * etc.)
 *
 * init_config IN - true if called as part of slurmctld initialization
 * Returns a Slurm errno.
 */
extern int bb_p_load_state(bool init_config)
{
	if (!init_config)
		return SLURM_SUCCESS;

	log_flag(BURST_BUF, "");
	_load_state(init_config); /* Has own locking */
	slurm_mutex_lock(&bb_state.bb_mutex);
	bb_set_tres_pos(&bb_state);
	_purge_vestigial_bufs();
	slurm_mutex_unlock(&bb_state.bb_mutex);

	_save_bb_state(); /* Has own locks excluding file write */

	return SLURM_SUCCESS;
}

/*
 * Return string containing current burst buffer status
 * argc IN - count of status command arguments
 * argv IN - status command arguments
 * uid - authenticated UID
 * gid - authenticated GID
 * RET status string, release memory using xfree()
 */
extern char *bb_p_get_status(uint32_t argc, char **argv, uint32_t uid,
			     uint32_t gid)
{
	char **pass_argv;
	char *status_resp = NULL;
	int rc;
	uint32_t pass_argc;
	run_lua_args_t run_lua_args;
	DEF_TIMERS;

	pass_argc = argc + 2;
	pass_argv = xcalloc(pass_argc + 1, sizeof(char *));
	pass_argv[0] = xstrdup_printf("%u", uid);
	pass_argv[1] = xstrdup_printf("%u", gid);
	for (int i = 0; i < argc; i++)
		pass_argv[i + 2] = xstrdup(argv[i]);

	memset(&run_lua_args, 0, sizeof run_lua_args);
	run_lua_args.argc = pass_argc;
	run_lua_args.argv = pass_argv;
	run_lua_args.lua_func = "slurm_bb_get_status";
	run_lua_args.resp_msg = &status_resp;
	run_lua_args.timeout = bb_state.bb_config.other_timeout;
	run_lua_args.with_scriptd = true;

	START_TIMER;
	rc = _run_lua_script(&run_lua_args);
	END_TIMER;
	log_flag(BURST_BUF, "slurm_bb_get_status ran for %s", TIME_STR);

	if (rc != SLURM_SUCCESS) {
		xfree(status_resp);
		status_resp = xstrdup("Error running slurm_bb_get_status\n");
	}

	xfree_array(pass_argv);

	return status_resp;
}

/*
 * Note configuration may have changed. Handle changes in BurstBufferParameters.
 *
 * Returns a Slurm errno.
 */
extern int bb_p_reconfig(void)
{
	/* read locks on assoc */
	assoc_mgr_lock_t assoc_locks =
		{ .assoc = READ_LOCK, .qos = READ_LOCK, .user = READ_LOCK };

	assoc_mgr_lock(&assoc_locks);
	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "");
	bb_load_config(&bb_state, (char *) plugin_type); /* Remove "const" */
	_test_config();

	/* reconfig is the place we make sure the pointers are correct */
	for (int i = 0; i < BB_HASH_SIZE; i++) {
		bb_alloc_t *bb_alloc = bb_state.bb_ahash[i];
		while (bb_alloc) {
			_set_assoc_mgr_ptrs(bb_alloc);
			bb_alloc = bb_alloc->next;
		}
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);
	assoc_mgr_unlock(&assoc_locks);

	return SLURM_SUCCESS;
}

/*
 * Pack current burst buffer state information for network transmission to
 * user (e.g. "scontrol show burst")
 *
 * Returns a Slurm errno.
 */
extern int bb_p_state_pack(uid_t uid, buf_t *buffer, uint16_t protocol_version)
{
	uint32_t rec_count = 0;

	slurm_mutex_lock(&bb_state.bb_mutex);
	packstr(bb_state.name, buffer);
	bb_pack_state(&bb_state, buffer, protocol_version);

	if (((bb_state.bb_config.flags & BB_FLAG_PRIVATE_DATA) == 0) ||
	    validate_operator(uid))
		uid = 0;	/* User can see all data */
	rec_count = bb_pack_bufs(uid, &bb_state, buffer, protocol_version);
	(void) bb_pack_usage(uid, &bb_state, buffer, protocol_version);
	log_flag(BURST_BUF, "record_count:%u", rec_count);
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Preliminary validation of a job submit request with respect to burst buffer
 * options. Performed after setting default account + qos, but prior to
 * establishing job ID or creating script file.
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_validate(job_desc_msg_t *job_desc, uid_t submit_uid,
			     char **err_msg)
{
	uint64_t bb_size = 0;
	int rc;

	xassert(job_desc);
	xassert(job_desc->tres_req_cnt);
	xassert(err_msg);

	rc = _parse_bb_opts(job_desc, &bb_size, submit_uid);
	if (rc != SLURM_SUCCESS)
		return rc;

	if ((job_desc->burst_buffer == NULL) ||
	    (job_desc->burst_buffer[0] == '\0'))
		return rc;

	log_flag(BURST_BUF, "job_user_id:%u, submit_uid:%u",
		 job_desc->user_id, submit_uid);
	log_flag(BURST_BUF, "burst_buffer:\n%s",
		 job_desc->burst_buffer);

	if (job_desc->user_id == 0) {
		info("User root can not allocate burst buffers");
		*err_msg = xstrdup("User root can not allocate burst buffers");
		return ESLURM_BURST_BUFFER_PERMISSION;
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.bb_config.allow_users) {
		bool found_user = false;
		for (int i = 0; bb_state.bb_config.allow_users[i]; i++) {
			if (job_desc->user_id ==
			    bb_state.bb_config.allow_users[i]) {
				found_user = true;
				break;
			}
		}
		if (!found_user) {
			*err_msg = xstrdup("User not found in AllowUsers");
			rc = ESLURM_BURST_BUFFER_PERMISSION;
			goto fini;
		}
	}

	if (bb_state.bb_config.deny_users) {
		bool found_user = false;
		for (int i = 0; bb_state.bb_config.deny_users[i]; i++) {
			if (job_desc->user_id ==
			    bb_state.bb_config.deny_users[i]) {
				found_user = true;
				break;
			}
		}
		if (found_user) {
			*err_msg = xstrdup("User found in DenyUsers");
			rc = ESLURM_BURST_BUFFER_PERMISSION;
			goto fini;
		}
	}

	if (bb_state.tres_pos > 0) {
		job_desc->tres_req_cnt[bb_state.tres_pos] =
			bb_size / (1024 * 1024);
	}

fini:
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/*
 * Secondary validation of a job submit request with respect to burst buffer
 * options. Performed after establishing job ID and creating script file.
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_validate2(job_record_t *job_ptr, char **err_msg)
{
	char *lua_func_name = "slurm_bb_job_process";
	uint32_t argc;
	char **argv;
	int rc = SLURM_SUCCESS, fd = -1, hash_inx;
	char *hash_dir = NULL, *job_dir = NULL, *script_file = NULL;
	char *task_script_file = NULL, *resp_msg = NULL;
	bool using_master_script = false;
	bb_job_t *bb_job;
	run_lua_args_t run_lua_args;
	DEF_TIMERS;

	/* Initialization */
	slurm_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.last_load_time == 0) {
		/* Assume request is valid for now, can't test it anyway */
		info("Burst buffer down, skip tests for %pJ",
		      job_ptr);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return SLURM_SUCCESS;
	}
	bb_job = _get_bb_job(job_ptr);
	if (bb_job == NULL) {
		/* No burst buffer specification */
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return SLURM_SUCCESS;
	}
	if (job_ptr->details->min_nodes == 0) {
		/*
		 * Since persistent burst buffers aren't allowed in this
		 * plugin, 0-node jobs are never allowed to have burst buffers.
		 */
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return ESLURM_INVALID_BURST_BUFFER_REQUEST;
	}

	log_flag(BURST_BUF, "%pJ", job_ptr);

	slurm_mutex_unlock(&bb_state.bb_mutex);

	/* Standard file location for job arrays */
	if ((job_ptr->array_task_id != NO_VAL) &&
	    (job_ptr->array_job_id != job_ptr->job_id)) {
		hash_inx = job_ptr->array_job_id % 10;
		xstrfmtcat(hash_dir, "%s/hash.%d",
			   slurm_conf.state_save_location, hash_inx);
		(void) mkdir(hash_dir, 0700);
		xstrfmtcat(job_dir, "%s/job.%u", hash_dir,
			   job_ptr->array_job_id);
		(void) mkdir(job_dir, 0700);
		xstrfmtcat(script_file, "%s/script", job_dir);
		fd = open(script_file, 0);
		if (fd >= 0) {	/* found the script */
			close(fd);
			using_master_script = true;
		} else {
			xfree(hash_dir);
		}
	} else {
		hash_inx = job_ptr->job_id % 10;
		xstrfmtcat(hash_dir, "%s/hash.%d",
			   slurm_conf.state_save_location, hash_inx);
		(void) mkdir(hash_dir, 0700);
		xstrfmtcat(job_dir, "%s/job.%u", hash_dir, job_ptr->job_id);
		(void) mkdir(job_dir, 0700);
		xstrfmtcat(script_file, "%s/script", job_dir);
		if (job_ptr->batch_flag == 0) {
			rc = bb_build_bb_script(job_ptr, script_file);
			if (rc != SLURM_SUCCESS) {
				/*
				 * There was an error writing to the script,
				 * and that error was logged by
				 * bb_build_bb_script(). Bail out now.
				 */
				goto fini;
			}
		}
	}


	/* Run "job_process" function, validates user script */
	argc = 3;
	argv = xcalloc(argc + 1, sizeof(char *));
	argv[0] = xstrdup_printf("%s", script_file);
	argv[1] = xstrdup_printf("%u", job_ptr->user_id);
	argv[2] = xstrdup_printf("%u", job_ptr->group_id);

	memset(&run_lua_args, 0, sizeof run_lua_args);
	run_lua_args.argc = argc;
	run_lua_args.argv = argv;
	run_lua_args.get_job_ptr = true;
	run_lua_args.have_job_lock = true;
	run_lua_args.job_id = job_ptr->job_id;
	run_lua_args.job_ptr = job_ptr;
	run_lua_args.lua_func = lua_func_name;
	run_lua_args.resp_msg = &resp_msg;

	START_TIMER;
	rc = _run_lua_script(&run_lua_args);
	END_TIMER;
	xfree_array(argv);
	log_flag(BURST_BUF, "%s for job %pJ ran for %s",
		 lua_func_name, job_ptr, TIME_STR);

	if (rc) {
		if (err_msg && resp_msg) {
			xfree(*err_msg);
			xstrfmtcat(*err_msg, "%s: %s",
				   plugin_type, resp_msg);
		}
		rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
	}
	xfree(resp_msg);

fini:
	/* Clean up */
	xfree(hash_dir);
	xfree(job_dir);
	if (rc != SLURM_SUCCESS) {
		slurm_mutex_lock(&bb_state.bb_mutex);
		bb_job_del(&bb_state, job_ptr->job_id);
		slurm_mutex_unlock(&bb_state.bb_mutex);
	} else if (using_master_script) {
		/*
		 * Job arrays need to have script file in the "standard"
		 * location for the remaining logic. Make hard link.
		 */
		hash_inx = job_ptr->job_id % 10;
		xstrfmtcat(hash_dir, "%s/hash.%d",
			   slurm_conf.state_save_location, hash_inx);
		(void) mkdir(hash_dir, 0700);
		xstrfmtcat(job_dir, "%s/job.%u", hash_dir, job_ptr->job_id);
		xfree(hash_dir);
		(void) mkdir(job_dir, 0700);
		xstrfmtcat(task_script_file, "%s/script", job_dir);
		xfree(job_dir);
		if ((link(script_file, task_script_file) != 0) &&
		    (errno != EEXIST)) {
			error("%s: link(%s,%s): %m",
			      __func__, script_file, task_script_file);
		}
	}
	xfree(task_script_file);
	xfree(script_file);

	return rc;
}

/*
 * Fill in the tres_cnt (in MB) based off the job record
 * NOTE: Based upon job-specific burst buffers, excludes persistent buffers
 * IN job_ptr - job record
 * IN/OUT tres_cnt - fill in this already allocated array with tres_cnts
 * IN locked - if the assoc_mgr tres read locked is locked or not
 */
extern void bb_p_job_set_tres_cnt(job_record_t *job_ptr, uint64_t *tres_cnt,
				  bool locked)
{
	bb_job_t *bb_job;

	if (!tres_cnt) {
		error("No tres_cnt given when looking at %pJ",
		      job_ptr);
	}

	if (bb_state.tres_pos < 0) {
		/* BB not defined in AccountingStorageTRES */
		return;
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	if ((bb_job = _get_bb_job(job_ptr))) {
		tres_cnt[bb_state.tres_pos] =
			bb_job->total_size / (1024 * 1024);
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);
}

/*
 * For a given job, return our best guess if when it might be able to start
 */
extern time_t bb_p_job_get_est_start(job_record_t *job_ptr)
{
	time_t est_start = time(NULL);
	bb_job_t *bb_job;
	int rc;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return est_start;

	if (job_ptr->array_recs &&
	    ((job_ptr->array_task_id == NO_VAL) ||
	     (job_ptr->array_task_id == INFINITE))) {
		/* Can't operate on job array. Guess 5 minutes. */
		est_start += 300;
		return est_start;
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.last_load_time == 0) {
		/*
		 * The plugin hasn't successfully loaded yet, so we can't know.
		 * Guess 1 hour.
		 */
		est_start += 3600;
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return est_start;
	}

	if (!(bb_job = _get_bb_job(job_ptr))) {
		/* No bb_job record; we can't know. */
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return est_start;
	}

	log_flag(BURST_BUF, "%pJ", job_ptr);

	if (bb_job->state == BB_STATE_PENDING) {
		if (bb_job->job_pool && bb_job->req_size)
			rc = bb_test_size_limit(job_ptr, bb_job, &bb_state,
						NULL);
		else
			rc = 0;

		if (rc == 0) { /* Could start now. */
			;
		} else if (rc == 1) { /* Exceeds configured limits */
			est_start += 365 * 24 * 60 * 60;
		} else {
			est_start = MAX(est_start, bb_state.next_end_time);
		}
	} else {
		/* Allocation or staging in progress, guess 1 minute from now */
		est_start++;
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return est_start;
}

/*
 * If the job (x) should be allocated a burst buffer, add it to the
 * job_candidates list (arg).
 */
static int _identify_bb_candidate(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	List job_candidates = (List) arg;
	bb_job_t *bb_job;
	bb_job_queue_rec_t *job_rec;

	if (!IS_JOB_PENDING(job_ptr) || (job_ptr->start_time == 0) ||
	    (job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return SLURM_SUCCESS;

	if (job_ptr->array_recs &&
	    ((job_ptr->array_task_id == NO_VAL) ||
	     (job_ptr->array_task_id == INFINITE)))
		return SLURM_SUCCESS; /* Can't operate on job array struct */

	bb_job = _get_bb_job(job_ptr);
	if (bb_job == NULL)
		return SLURM_SUCCESS;
	if (bb_job->state == BB_STATE_COMPLETE) {
		/* Job requeued or slurmctld restarted during stage-in */
		bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_PENDING);
	} else if (bb_job->state >= BB_STATE_POST_RUN) {
		/* Requeued job still staging out */
		return SLURM_SUCCESS;
	}
	job_rec = xmalloc(sizeof(bb_job_queue_rec_t));
	job_rec->job_ptr = job_ptr;
	job_rec->bb_job = bb_job;
	list_push(job_candidates, job_rec);
	return SLURM_SUCCESS;
}

/*
 * Purge files we have created for the job.
 * bb_state.bb_mutex is locked on function entry.
 * job_ptr may be NULL if not found
 */
static void _purge_bb_files(uint32_t job_id, job_record_t *job_ptr)
{
	char *hash_dir = NULL, *job_dir = NULL;
	char *script_file = NULL, *path_file = NULL;
	int hash_inx;

	hash_inx = job_id % 10;
	xstrfmtcat(hash_dir, "%s/hash.%d",
		   slurm_conf.state_save_location, hash_inx);
	(void) mkdir(hash_dir, 0700);
	xstrfmtcat(job_dir, "%s/job.%u", hash_dir, job_id);
	(void) mkdir(job_dir, 0700);

	xstrfmtcat(path_file, "%s/pathfile", job_dir);
	(void) unlink(path_file);
	xfree(path_file);

	if (!job_ptr || (job_ptr->batch_flag == 0)) {
		xstrfmtcat(script_file, "%s/script", job_dir);
		(void) unlink(script_file);
		xfree(script_file);
	}

	(void) unlink(job_dir);
	xfree(job_dir);
	xfree(hash_dir);
}

static void *_start_teardown(void *x)
{
	int rc, retry_count = 0;
	uint32_t timeout, argc;
	char *resp_msg = NULL;
	char **argv;
	bool track_script_signal = false;
	teardown_args_t *teardown_args = (teardown_args_t *)x;
	job_record_t *job_ptr;
	bb_alloc_t *bb_alloc = NULL;
	bb_job_t *bb_job = NULL;
	/* Locks: write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	run_lua_args_t run_lua_args;
	DEF_TIMERS;
	DEF_STAGE_THROTTLE;
	_stage_throttle_start(&stage_cnt_mutex, &stage_cnt_cond, &stage_cnt);

	argc = 5;
	argv = xcalloc(argc + 1, sizeof(char *)); /* NULL-terminated */
	argv[0] = xstrdup_printf("%u", teardown_args->job_id);
	argv[1] = xstrdup_printf("%s", teardown_args->job_script);
	argv[2] = xstrdup_printf("%s", teardown_args->hurry ? "true" : "false");
	argv[3] = xstrdup_printf("%u", teardown_args->user_id);
	argv[4] = xstrdup_printf("%u", teardown_args->group_id);

	timeout = bb_state.bb_config.other_timeout;

	memset(&run_lua_args, 0, sizeof run_lua_args);
	run_lua_args.argc = argc;
	run_lua_args.argv = argv;
	run_lua_args.job_id = teardown_args->job_id;
	run_lua_args.lua_func = "slurm_bb_job_teardown";
	run_lua_args.resp_msg = &resp_msg;
	run_lua_args.timeout = timeout;
	run_lua_args.track_script_signal = &track_script_signal;
	run_lua_args.with_scriptd = true;

	/* Run lua "teardown" function */
	while (1) {
		START_TIMER;
		rc = _run_lua_script(&run_lua_args);
		END_TIMER;
		log_flag(BURST_BUF, "Teardown for JobId=%u ran for %s",
			 teardown_args->job_id, TIME_STR);

		if (track_script_signal) {
			/* Killed by slurmctld, exit now. */
			info("teardown for JobId=%u terminated by slurmctld",
			     teardown_args->job_id);
			goto fini;
		}

		if (rc != SLURM_SUCCESS) {
			int sleep_time = 10; /* Arbitrary time */

			/*
			 * To prevent an infinite loop of teardown failures,
			 * limit the number of times we retry teardown and
			 * sleep in between tries.
			 * Give up trying teardown if it fails after retrying
			 * a certain number of times.
			 */
			trigger_burst_buffer();
			if (retry_count >= MAX_RETRY_CNT) {
				error("Teardown for JobId=%u failed %d times. We won't retry teardown anymore. Removing burst buffer.",
				      teardown_args->job_id, retry_count);
				break;
			} else {
				error("Teardown for JobId=%u failed. status: %d, response: %s. Retrying after %d seconds. Current retry count=%d, max retries=%d",
				      teardown_args->job_id, rc, resp_msg,
				      sleep_time, retry_count, MAX_RETRY_CNT);
				retry_count++;

				lock_slurmctld(job_write_lock);
				job_ptr =
					find_job_record(teardown_args->job_id);
				if (job_ptr) {
					job_ptr->state_reason =
						FAIL_BURST_BUFFER_OP;
					xfree(job_ptr->state_desc);
					xstrfmtcat(job_ptr->state_desc, "%s: teardown: %s",
						   plugin_type, resp_msg);
					bb_update_system_comment(job_ptr,
								 "teardown",
								 resp_msg, 0);
				}
				unlock_slurmctld(job_write_lock);
				sleep(sleep_time);
			}
		} else {
			break; /* Success, break out of loop */
		}
	}

	lock_slurmctld(job_write_lock);
	slurm_mutex_lock(&bb_state.bb_mutex);
	job_ptr = find_job_record(teardown_args->job_id);
	_purge_bb_files(teardown_args->job_id, job_ptr);
	if (job_ptr) {
		if ((bb_alloc = bb_find_alloc_rec(&bb_state, job_ptr))){
			bb_limit_rem(bb_alloc->user_id, bb_alloc->size,
				     bb_alloc->pool, &bb_state);
			(void) bb_free_alloc_rec(&bb_state, bb_alloc);
		}
		if ((bb_job = _get_bb_job(job_ptr)))
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_COMPLETE);
		job_ptr->job_state &= (~JOB_STAGE_OUT);
		if (!IS_JOB_PENDING(job_ptr) &&	/* No email if requeue */
		    (job_ptr->mail_type & MAIL_JOB_STAGE_OUT)) {
			mail_job_info(job_ptr, MAIL_JOB_STAGE_OUT);
			job_ptr->mail_type &= (~MAIL_JOB_STAGE_OUT);
		}
	} else {
		/*
		 * This will happen when slurmctld restarts and needs
		 * to clear vestigial buffers
		 */
		char buf_name[32];
		snprintf(buf_name, sizeof(buf_name), "%u",
			 teardown_args->job_id);
		bb_alloc = bb_find_name_rec(buf_name,
					    teardown_args->user_id,
					    &bb_state);
		if (bb_alloc) {
			bb_limit_rem(bb_alloc->user_id, bb_alloc->size,
				     bb_alloc->pool, &bb_state);
			(void) bb_free_alloc_rec(&bb_state, bb_alloc);
		}

	}
	slurm_mutex_unlock(&bb_state.bb_mutex);
	unlock_slurmctld(job_write_lock);

fini:
	_stage_throttle_fini(&stage_cnt_mutex, &stage_cnt_cond, &stage_cnt);
	xfree(resp_msg);
	xfree(teardown_args->job_script);
	xfree(teardown_args);
	xfree_array(argv);

	return NULL;
}

static void _queue_teardown(uint32_t job_id, uint32_t user_id, bool hurry,
			    uint32_t group_id)
{
	char *hash_dir = NULL, *job_script = NULL;
	int hash_inx = job_id % 10;
	struct stat buf;
	teardown_args_t *teardown_args;
	pthread_t tid;

	xstrfmtcat(hash_dir, "%s/hash.%d",
		   slurm_conf.state_save_location, hash_inx);
	xstrfmtcat(job_script, "%s/job.%u/script", hash_dir, job_id);
	if (stat(job_script, &buf) == -1) {
		int fd = creat(job_script, 0755);
		if (fd >= 0) {
			int len;
			char *dummy_script = "#!/bin/bash\nexit 0\n";
			len = strlen(dummy_script) + 1;
			if (write(fd, dummy_script, len) != len) {
				verbose("%s: write(%s): %m",
					__func__, job_script);
			}
			close(fd);
		}
	}

	teardown_args = xmalloc(sizeof *teardown_args);
	teardown_args->job_id = job_id;
	teardown_args->user_id = user_id;
	teardown_args->group_id = group_id;
	teardown_args->job_script = job_script;
	teardown_args->hurry = hurry;

	slurm_thread_create_detached(&tid, _start_teardown, teardown_args);

	xfree(hash_dir);
}

static void *_start_stage_in(void *x)
{
	int rc;
	uint32_t timeout, argc;
	bool get_real_size = false, track_script_signal = false;
	char *resp_msg = NULL, *op = NULL;
	char **argv;
	long real_size = 0;
	stage_in_args_t *stage_in_args = (stage_in_args_t *) x;
	job_record_t *job_ptr;
	bb_alloc_t *bb_alloc = NULL;
	bb_job_t *bb_job;
	slurmctld_lock_t job_write_lock = { .job = WRITE_LOCK };
	run_lua_args_t run_lua_args;

	DEF_TIMERS;

	argc = 6;
	argv = xcalloc(argc + 1, sizeof (char *)); /* NULL-terminated */
	argv[0] = xstrdup_printf("%u", stage_in_args->job_id);
	argv[1] = xstrdup_printf("%u", stage_in_args->uid);
	argv[2] = xstrdup_printf("%u", stage_in_args->gid);
	argv[3] = xstrdup_printf("%s", stage_in_args->pool);
	argv[4] = xstrdup_printf("%"PRIu64, stage_in_args->bb_size);
	argv[5] = xstrdup_printf("%s", stage_in_args->job_script);

	timeout = bb_state.bb_config.other_timeout;
	op = "slurm_bb_setup";

	memset(&run_lua_args, 0, sizeof run_lua_args);
	run_lua_args.argc = argc;
	run_lua_args.argv = argv;
	run_lua_args.get_job_ptr = true;
	run_lua_args.job_id = stage_in_args->job_id;
	run_lua_args.lua_func = op;
	run_lua_args.resp_msg = &resp_msg;
	run_lua_args.timeout = timeout;
	run_lua_args.track_script_signal = &track_script_signal;
	run_lua_args.with_scriptd = true;

	START_TIMER;
	rc = _run_lua_script(&run_lua_args);
	END_TIMER;
	log_flag(BURST_BUF, "%s for job JobId=%u ran for %s",
		 op, stage_in_args->job_id, TIME_STR);

	if (track_script_signal) {
		/* Killed by slurmctld, exit now. */
		info("setup for JobId=%u terminated by slurmctld",
		     stage_in_args->job_id);
		goto fini;
	}

	if (rc != SLURM_SUCCESS) {
		trigger_burst_buffer();
		error("setup for JobId=%u failed.", stage_in_args->job_id);
		rc = SLURM_ERROR;
		lock_slurmctld(job_write_lock);
		job_ptr = find_job_record(stage_in_args->job_id);
		if (job_ptr)
			bb_update_system_comment(job_ptr, "setup", resp_msg, 0);
		unlock_slurmctld(job_write_lock);
	}

	if (rc == SLURM_SUCCESS) {
		xfree(resp_msg);
		xfree_array(argv);
		argc = 4;
		argv = xcalloc(argc + 1, sizeof (char *)); /* NULL-terminated */
		argv[0] = xstrdup_printf("%u", stage_in_args->job_id);
		argv[1] = xstrdup_printf("%s", stage_in_args->job_script);
		argv[2] = xstrdup_printf("%u", stage_in_args->uid);
		argv[3] = xstrdup_printf("%u", stage_in_args->gid);


		timeout = bb_state.bb_config.stage_in_timeout;
		op = "slurm_bb_data_in";

		memset(&run_lua_args, 0, sizeof run_lua_args);
		run_lua_args.argc = argc;
		run_lua_args.argv = argv;
		run_lua_args.get_job_ptr = true;
		run_lua_args.job_id = stage_in_args->job_id;
		run_lua_args.lua_func = op;
		run_lua_args.resp_msg = &resp_msg;
		run_lua_args.timeout = timeout;
		run_lua_args.track_script_signal = &track_script_signal;
		run_lua_args.with_scriptd = true;

		START_TIMER;
		rc = _run_lua_script(&run_lua_args);
		END_TIMER;
		log_flag(BURST_BUF, "%s for JobId=%u ran for %s",
			 op, stage_in_args->job_id, TIME_STR);

		if (track_script_signal) {
			/* Killed by slurmctld, exit now. */
			info("data_in for JobId=%u terminated by slurmctld",
			     stage_in_args->job_id);
			goto fini;
		}

		if (rc != SLURM_SUCCESS) {
			trigger_burst_buffer();
			error("slurm_bb_data_in for JobId=%u failed.",
			      stage_in_args->job_id);
			rc = SLURM_ERROR;
			lock_slurmctld(job_write_lock);
			job_ptr = find_job_record(stage_in_args->job_id);
			if (job_ptr)
				bb_update_system_comment(job_ptr, "data_in",
							 resp_msg, 0);
			unlock_slurmctld(job_write_lock);
		}
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	bb_job = bb_job_find(&bb_state, stage_in_args->job_id);
	if ((rc == SLURM_SUCCESS) && bb_job && bb_job->req_size)
		get_real_size = true;
	slurm_mutex_unlock(&bb_state.bb_mutex);

	if (get_real_size) {
		xfree(resp_msg);
		xfree_array(argv);
		argc = 3;
		argv = xcalloc(argc + 1, sizeof(char *)); /* NULL terminated */
		argv[0] = xstrdup_printf("%u", stage_in_args->job_id);
		argv[1] = xstrdup_printf("%u", stage_in_args->uid);
		argv[2] = xstrdup_printf("%u", stage_in_args->gid);

		START_TIMER;
		op = "slurm_bb_real_size";

		memset(&run_lua_args, 0, sizeof run_lua_args);
		run_lua_args.argc = argc;
		run_lua_args.argv = argv;
		run_lua_args.get_job_ptr = true;
		run_lua_args.job_id = stage_in_args->job_id;
		run_lua_args.lua_func = op;
		run_lua_args.resp_msg = &resp_msg;
		run_lua_args.timeout = timeout;
		run_lua_args.track_script_signal = &track_script_signal;
		run_lua_args.with_scriptd = true;

		rc = _run_lua_script(&run_lua_args);
		END_TIMER;
		log_flag(BURST_BUF, "%s for JobId=%u ran for %s",
			 op, stage_in_args->job_id, TIME_STR);

		if (track_script_signal) {
			/* Killed by slurmctld, exit now. */
			info("%s for JobId=%u terminated by slurmctld",
			     op, stage_in_args->job_id);
			goto fini;
		}

		if (rc != SLURM_SUCCESS) {
			error("%s for JobId=%u failed, status:%u, response:%s",
			      op, stage_in_args->job_id, rc, resp_msg);
		} else if (resp_msg) {
			char *end_ptr;

			real_size = strtol(resp_msg, &end_ptr, 10);
			if ((real_size < 0) || (real_size == LONG_MAX) ||
			    (end_ptr == resp_msg)) {
				error("%s return value=\"%s\" is invalid, discarding result",
				      op, resp_msg);
				real_size = 0;
			}
		}
	}

	lock_slurmctld(job_write_lock);
	slurm_mutex_lock(&bb_state.bb_mutex);
	job_ptr = find_job_record(stage_in_args->job_id);
	if (!job_ptr) {
		error("unable to find job record for JobId=%u",
		      stage_in_args->job_id);
	} else if (rc == SLURM_SUCCESS) {
		bb_job = bb_job_find(&bb_state, stage_in_args->job_id);
		if (bb_job)
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_STAGED_IN);
		if (bb_job && bb_job->total_size) {
			/*
			 * Adjust total size to real size if real size
			 * returns something bigger.
			 */
			if (((uint64_t)real_size) > bb_job->req_size) {
				info("%pJ total_size increased from %"PRIu64" to %ld",
				     job_ptr,
				     bb_job->req_size, real_size);
				bb_job->total_size = real_size;
				bb_limit_rem(stage_in_args->uid,
					     stage_in_args->bb_size,
					     stage_in_args->pool, &bb_state);
				/* Restore limit based upon actual size. */
				bb_limit_add(stage_in_args->uid,
					     bb_job->total_size,
					     stage_in_args->pool, &bb_state,
					     true);
			}
			bb_alloc = bb_find_alloc_rec(&bb_state, job_ptr);
			if (bb_alloc) {
				if (bb_alloc->size != bb_job->total_size) {
					/*
					 * bb_alloc is state saved, so we need
					 * to update bb_alloc in case slurmctld
					 * restarts.
					 */
					bb_alloc->size = bb_job->total_size;
					bb_state.last_update_time = time(NULL);
				}
				log_flag(BURST_BUF, "Setup/stage-in complete for %pJ",
					 job_ptr);
				queue_job_scheduler();
			} else {
				error("unable to find bb_alloc record for %pJ",
				      job_ptr);
			}
		}
	} else {
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
		xstrfmtcat(job_ptr->state_desc, "%s: %s: %s",
			   plugin_type, op, resp_msg);
		job_ptr->priority = 0; /* Hold job */
		if (bb_state.bb_config.flags & BB_FLAG_TEARDOWN_FAILURE) {
			bb_job = bb_job_find(&bb_state, stage_in_args->job_id);
			if (bb_job)
				bb_set_job_bb_state(job_ptr, bb_job,
						    BB_STATE_TEARDOWN);
			_queue_teardown(job_ptr->job_id,
					job_ptr->user_id, true,
					job_ptr->group_id);
		}
	}
	stage_in_cnt--;
	slurm_mutex_unlock(&bb_state.bb_mutex);
	unlock_slurmctld(job_write_lock);

fini:
	xfree(resp_msg);
	xfree(stage_in_args->job_script);
	xfree(stage_in_args->pool);
	xfree(stage_in_args);
	xfree_array(argv);

	return NULL;
}

static int _queue_stage_in(job_record_t *job_ptr, bb_job_t *bb_job)
{
	char *hash_dir = NULL, *job_dir = NULL;
	int hash_inx = job_ptr->job_id % 10;
	stage_in_args_t *stage_in_args;
	bb_alloc_t *bb_alloc = NULL;
	pthread_t tid;

	xstrfmtcat(hash_dir, "%s/hash.%d",
		   slurm_conf.state_save_location, hash_inx);
	(void) mkdir(hash_dir, 0700);
	xstrfmtcat(job_dir, "%s/job.%u", hash_dir, job_ptr->job_id);

	stage_in_args = xmalloc(sizeof *stage_in_args);
	stage_in_args->job_id = job_ptr->job_id;
	stage_in_args->uid = job_ptr->user_id;
	stage_in_args->gid = job_ptr->group_id;
	if (bb_job->job_pool)
		stage_in_args->pool = xstrdup(bb_job->job_pool);
	else
		stage_in_args->pool = NULL;
	stage_in_args->bb_size = bb_job->total_size;
	stage_in_args->job_script = bb_handle_job_script(job_ptr, bb_job);

	/*
	 * Create bb allocation for the job now. Check if it has already been
	 * created (perhaps it was created but then slurmctld restarted).
	 * bb_alloc is the structure that is state saved.
	 * If we wait until the _start_stage_in thread to create bb_alloc,
	 * we introduce a race condition where the thread could be killed
	 * (if slurmctld is shut down) before the thread creates
	 * bb_alloc. That race would mean the burst buffer isn't state saved.
	 */
	if (!(bb_alloc = bb_find_alloc_rec(&bb_state, job_ptr))) {
		bb_alloc = bb_alloc_job(&bb_state, job_ptr, bb_job);
		bb_alloc->create_time = time(NULL);
	}
	bb_limit_add(job_ptr->user_id, bb_job->total_size, bb_job->job_pool,
		     &bb_state, true);

	stage_in_cnt++;
	slurm_thread_create_detached(&tid, _start_stage_in, stage_in_args);

	xfree(hash_dir);
	xfree(job_dir);

	return SLURM_SUCCESS;
}

static void _alloc_job_bb(job_record_t *job_ptr, bb_job_t *bb_job,
			  bool job_ready)
{
	log_flag(BURST_BUF, "start job allocate %pJ", job_ptr);

	bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_STAGING_IN);
	_queue_stage_in(job_ptr, bb_job);
}

static int _try_alloc_job_bb(void *x, void *arg)
{
	bb_job_queue_rec_t *job_rec = (bb_job_queue_rec_t *) x;
	job_record_t *job_ptr = job_rec->job_ptr;
	bb_job_t *bb_job = job_rec->bb_job;
	int rc;

	if (bb_job->state >= BB_STATE_STAGING_IN)
		return SLURM_SUCCESS; /* Job was already allocated a buffer */

	if (bb_job->job_pool && bb_job->req_size)
		rc = bb_test_size_limit(job_ptr, bb_job, &bb_state, NULL);
	else
		rc = 0;

	if (stage_in_cnt >= MAX_BURST_BUFFERS_PER_STAGE)
		return SLURM_ERROR; /* Break out of loop */

	if (rc == 0) {
		/*
		 * Job could start now. Allocate burst buffer and continue to
		 * the next job.
		 */
		_alloc_job_bb(job_ptr, bb_job, true);
		rc = SLURM_SUCCESS;
	} else if (rc == 1) /* Exceeds configured limits, try next job */
		rc = SLURM_SUCCESS;
	else /* No space currently available, break out of loop */
		rc = SLURM_ERROR;

	return rc;
}

/*
 * Attempt to allocate resources and begin file staging for pending jobs.
 */
extern int bb_p_job_try_stage_in(List job_queue)
{
	List job_candidates;

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "Mutex locked");

	if (bb_state.last_load_time == 0) {
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return SLURM_SUCCESS;
	}

	/* Identify candidates to be allocated burst buffers */
	job_candidates = list_create(xfree_ptr);
	list_for_each(job_queue, _identify_bb_candidate, job_candidates);

	/* Sort in order of expected start time */
	list_sort(job_candidates, bb_job_queue_sort);

	/* Try to allocate burst buffers for these jobs. */
	list_for_each(job_candidates, _try_alloc_job_bb, NULL);

	slurm_mutex_unlock(&bb_state.bb_mutex);
	FREE_NULL_LIST(job_candidates);

	return SLURM_SUCCESS;
}

/*
 * Determine if a job's burst buffer stage-in is complete
 * job_ptr IN - Job to test
 * test_only IN - If false, then attempt to allocate burst buffer if possible
 *
 * RET: 0 - stage-in is underway
 *      1 - stage-in complete
 *     -1 - stage-in not started or burst buffer in some unexpected state
 */
extern int bb_p_job_test_stage_in(job_record_t *job_ptr, bool test_only)
{
	bb_job_t *bb_job = NULL;
	int rc = 1;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return 1;

	if (job_ptr->array_recs &&
	    ((job_ptr->array_task_id == NO_VAL) ||
	     (job_ptr->array_task_id == INFINITE)))
		return -1;	/* Can't operate on job array structure */

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "%pJ test_only:%d",
		 job_ptr, (int) test_only);
	if (bb_state.last_load_time != 0)
		bb_job = _get_bb_job(job_ptr);
	if (bb_job && (bb_job->state == BB_STATE_COMPLETE))
		bb_set_job_bb_state(job_ptr, bb_job,
				    BB_STATE_PENDING); /* job requeued */
	if (bb_job == NULL) {
		rc = -1;
	} else if (bb_job->state < BB_STATE_STAGING_IN) {
		/* Job buffer not allocated, create now if space available */
		rc = -1;
		if (stage_in_cnt >= MAX_BURST_BUFFERS_PER_STAGE)
			goto fini;
		if (test_only)
			goto fini;
		if (bb_job->job_pool && bb_job->req_size) {
			if ((bb_test_size_limit(job_ptr, bb_job, &bb_state,
						NULL) == 0)) {
				_alloc_job_bb(job_ptr, bb_job, false);
				rc = 0; /* Setup/stage-in in progress */
			}
		} else {
			_alloc_job_bb(job_ptr, bb_job, false);
			rc = 0; /* Setup/stage-in in progress */
		}
	} else if (bb_job->state == BB_STATE_STAGING_IN) {
		rc = 0;
	} else if (bb_job->state == BB_STATE_STAGED_IN) {
		rc = 1;
	} else {
		rc = -1;	/* Requeued job still staging in */
	}

fini:
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/* Add key=value pairs from file_path to the job's environment */
static void _update_job_env(job_record_t *job_ptr, char *file_path)
{
	struct stat stat_buf;
	char *data_buf = NULL, *start, *sep;
	int path_fd, i, inx = 0, env_cnt = 0;
	ssize_t read_size;

	/* Read the environment variables file */
	path_fd = open(file_path, 0);
	if (path_fd == -1) {
		error("open error on file %s: %m",
		      file_path);
		return;
	}
	fd_set_close_on_exec(path_fd);
	if (fstat(path_fd, &stat_buf) == -1) {
		error("stat error on file %s: %m",
		      file_path);
		stat_buf.st_size = 2048;
	} else if (stat_buf.st_size == 0)
		goto fini;
	data_buf = xmalloc_nz(stat_buf.st_size + 1);
	while (inx < stat_buf.st_size) {
		read_size = read(path_fd, data_buf + inx, stat_buf.st_size);
		if (read_size < 0)
			data_buf[inx] = '\0';
		else
			data_buf[inx + read_size] = '\0';
		if (read_size > 0) {
			inx += read_size;
		} else if (read_size == 0) {	/* EOF */
			break;
		} else if (read_size < 0) {	/* error */
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			error("read error on file %s: %m",
			      file_path);
			break;
		}
	}
	log_flag(BURST_BUF, "%s", data_buf);

	/* Get count of environment variables in the file */
	env_cnt = 0;
	if (data_buf) {
		for (i = 0; data_buf[i]; i++) {
			if (data_buf[i] == '=')
				env_cnt++;
		}
	}

	/* Add to supplemental environment variables (in job record) */
	if (env_cnt) {
		xrecalloc(job_ptr->details->env_sup,
			  MAX(job_ptr->details->env_cnt + env_cnt, 1 + env_cnt),
			  sizeof(char *));
		start = data_buf;
		for (i = 0; (i < env_cnt) && start[0]; i++) {
			sep = strchr(start, '\n');
			if (sep)
				sep[0] = '\0';
			job_ptr->details->env_sup[job_ptr->details->env_cnt++] =
				xstrdup(start);
			if (sep)
				start = sep + 1;
			else
				break;
		}
	}

fini:	xfree(data_buf);
	close(path_fd);
}

/* Kill job from CONFIGURING state */
static void _kill_job(job_record_t *job_ptr, bool hold_job)
{
	last_job_update = time(NULL);
	job_ptr->end_time = last_job_update;
	if (hold_job)
		job_ptr->priority = 0;
	build_cg_bitmap(job_ptr);
	job_ptr->exit_code = 1;
	job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
	xfree(job_ptr->state_desc);
	job_ptr->state_desc = xstrdup("Burst buffer pre_run error");

	job_ptr->job_state  = JOB_REQUEUE;
	job_completion_logger(job_ptr, true);
	job_ptr->job_state = JOB_PENDING | JOB_COMPLETING;

	deallocate_nodes(job_ptr, false, false, false);
}

static void *_start_pre_run(void *x)
{
	int rc;
	uint32_t timeout, argc;
	bool nodes_ready = false, run_kill_job = false, hold_job = false;
	bool track_script_signal = false;
	char *resp_msg = NULL, *op;
	char **argv;
	bb_job_t *bb_job = NULL;
	job_record_t *job_ptr;
	/* Locks: read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };
	pre_run_args_t *pre_run_args = (pre_run_args_t *) x;
	run_lua_args_t run_lua_args;
	DEF_TIMERS;
	DEF_STAGE_THROTTLE;
	_stage_throttle_start(&stage_cnt_mutex, &stage_cnt_cond, &stage_cnt);

	argc = 4;
	argv = xcalloc(argc + 1, sizeof (char *)); /* NULL-terminated */
	argv[0] = xstrdup_printf("%u", pre_run_args->job_id);
	argv[1] = xstrdup_printf("%s", pre_run_args->job_script);
	argv[2] = xstrdup_printf("%u", pre_run_args->uid);
	argv[3] = xstrdup_printf("%u", pre_run_args->gid);

	/* Wait for node boot to complete. */
	while (!nodes_ready) {
		lock_slurmctld(job_read_lock);
		job_ptr = find_job_record(pre_run_args->job_id);
		if (!job_ptr || IS_JOB_COMPLETED(job_ptr)) {
			unlock_slurmctld(job_read_lock);
			goto fini;
		}
		if (test_job_nodes_ready(job_ptr))
			nodes_ready = true;
		unlock_slurmctld(job_read_lock);
		if (!nodes_ready)
			sleep(60);
	}

	timeout = pre_run_args->timeout;
	op = "slurm_bb_pre_run";

	memset(&run_lua_args, 0, sizeof run_lua_args);
	run_lua_args.argc = argc;
	run_lua_args.argv = argv;
	run_lua_args.get_job_ptr = true;
	run_lua_args.job_id = pre_run_args->job_id;
	run_lua_args.lua_func = op;
	run_lua_args.resp_msg = &resp_msg;
	run_lua_args.timeout = timeout;
	run_lua_args.track_script_signal = &track_script_signal;
	run_lua_args.with_scriptd = true;

	START_TIMER;
	rc = _run_lua_script(&run_lua_args);
	END_TIMER;

	if (track_script_signal) {
		/* Killed by slurmctld, exit now. */
		info("%s for JobId=%u terminated by slurmctld",
		     op, pre_run_args->job_id);
		goto fini;
	}

	lock_slurmctld(job_write_lock);
	slurm_mutex_lock(&bb_state.bb_mutex);
	job_ptr = find_job_record(pre_run_args->job_id);
	log_flag(BURST_BUF, "%s for %pJ ran for %s", op, job_ptr, TIME_STR);

	if (job_ptr)
		bb_job = _get_bb_job(job_ptr);
	if (rc != SLURM_SUCCESS) {
		/* pre_run failure */
		trigger_burst_buffer();
		error("%s failed for JobId=%u", op, pre_run_args->job_id);
		if (job_ptr) {
			bb_update_system_comment(job_ptr, "pre_run", resp_msg,
						 0);
			if (IS_JOB_RUNNING(job_ptr))
				run_kill_job = true;
			if (bb_job) {
				bb_set_job_bb_state(job_ptr, bb_job,
						    BB_STATE_TEARDOWN);
				if (bb_job->retry_cnt++ > MAX_RETRY_CNT)
					hold_job = true;
			}
		}
		_queue_teardown(pre_run_args->job_id, pre_run_args->uid, true,
				pre_run_args->gid);
	} else if (bb_job) {
		/* pre_run success and the job's BB record exists */
		if (bb_job->state == BB_STATE_ALLOC_REVOKE)
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_STAGED_IN);
		else
			bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_RUNNING);
	}
	if (job_ptr) {
		if (run_kill_job)
			job_ptr->job_state &= ~JOB_CONFIGURING;
		prolog_running_decr(job_ptr);
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);
	if (run_kill_job) {
		/* bb_mutex must be unlocked before calling this */
		_kill_job(job_ptr, hold_job);
	}
	unlock_slurmctld(job_write_lock);

fini:
	_stage_throttle_fini(&stage_cnt_mutex, &stage_cnt_cond, &stage_cnt);
	xfree(resp_msg);
	xfree(pre_run_args->job_script);
	xfree(pre_run_args);
	xfree_array(argv);

	return NULL;
}

/* Attempt to claim burst buffer resources.
 * At this time, bb_g_job_test_stage_in() should have been run successfully AND
 * the compute nodes selected for the job.
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_begin(job_record_t *job_ptr)
{
	char *path_file = NULL;
	char *job_dir = NULL, *resp_msg = NULL, *job_script = NULL;
	int hash_inx = job_ptr->job_id % 10;
	int rc = SLURM_SUCCESS;
	uint32_t argc;
	char **argv;
	pthread_t tid;
	bb_job_t *bb_job;
	pre_run_args_t *pre_run_args;
	run_lua_args_t run_lua_args;
	DEF_TIMERS;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return SLURM_SUCCESS;

	if (!job_ptr->job_resrcs || !job_ptr->job_resrcs->nodes) {
		error("%pJ lacks node allocation",
		      job_ptr);
		return SLURM_ERROR;
	}

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "%pJ", job_ptr);

	if (bb_state.last_load_time == 0) {
		info("Burst buffer down, can not start %pJ",
		      job_ptr);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return SLURM_ERROR;
	}
	bb_job = _get_bb_job(job_ptr);
	if (!bb_job) {
		error("no job record buffer for %pJ", job_ptr);
		xfree(job_ptr->state_desc);
		job_ptr->state_desc =
			xstrdup("Could not find burst buffer record");
		job_ptr->state_reason = FAIL_BURST_BUFFER_OP;
		_queue_teardown(job_ptr->job_id, job_ptr->user_id, true,
				job_ptr->group_id);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return SLURM_ERROR;
	}
	xstrfmtcat(job_dir, "%s/hash.%d/job.%u",
		   slurm_conf.state_save_location, hash_inx, job_ptr->job_id);
	bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_PRE_RUN);

	slurm_mutex_unlock(&bb_state.bb_mutex);

	xstrfmtcat(job_script, "%s/script", job_dir);

	/* Create an empty "path" file which can be used by lua. */
	xstrfmtcat(path_file, "%s/path", job_dir);
	bb_write_file(path_file, "");
	/* Initialize args and run the "paths" function. */
	argc = 5;
	argv = xcalloc(argc + 1, sizeof (char *)); /* NULL-terminated */
	argv[0] = xstrdup_printf("%u", job_ptr->job_id);
	argv[1] = xstrdup_printf("%s", job_script);
	argv[2] = xstrdup_printf("%s", path_file);
	argv[3] = xstrdup_printf("%u", job_ptr->user_id);
	argv[4] = xstrdup_printf("%u", job_ptr->group_id);

	memset(&run_lua_args, 0, sizeof run_lua_args);
	run_lua_args.argc = argc;
	run_lua_args.argv = argv;
	run_lua_args.get_job_ptr = true;
	run_lua_args.have_job_lock = true;
	run_lua_args.job_id = job_ptr->job_id;
	run_lua_args.job_ptr = job_ptr;
	run_lua_args.lua_func = "slurm_bb_paths";
	run_lua_args.resp_msg = &resp_msg;
	run_lua_args.timeout = 0;

	START_TIMER;
	rc = _run_lua_script(&run_lua_args);
	END_TIMER;
	log_flag(BURST_BUF, "slurm_bb_paths ran for %s", TIME_STR);

	/* resp_msg already logged by _run_lua_script. */
	xfree(resp_msg);
	xfree_array(argv);

	if (rc != SLURM_SUCCESS) {
		error("paths for %pJ failed", job_ptr);
		rc = ESLURM_INVALID_BURST_BUFFER_REQUEST;
		goto fini;
	} else {
		_update_job_env(job_ptr, path_file);
	}

	/* Setup for the "pre_run" function. */
	pre_run_args = xmalloc(sizeof *pre_run_args);
	pre_run_args->job_id = job_ptr->job_id;
	pre_run_args->job_script = job_script; /* Point at malloc'd string */
	job_script = NULL; /* Avoid two variables pointing at the same string */
	pre_run_args->timeout = bb_state.bb_config.other_timeout;
	pre_run_args->uid = job_ptr->user_id;
	pre_run_args->gid = job_ptr->group_id;
	if (job_ptr->details) { /* Defer launch until completion */
		job_ptr->details->prolog_running++;
		job_ptr->job_state |= JOB_CONFIGURING;
	}

	slurm_thread_create_detached(&tid, _start_pre_run, pre_run_args);

fini:
	xfree(job_script);
	xfree(path_file);
	xfree(job_dir);

	return rc;
}

/* Revoke allocation, but do not release resources.
 * Executed after bb_p_job_begin() if there was an allocation failure.
 * Does not release previously allocated resources.
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_revoke_alloc(job_record_t *job_ptr)
{
	bb_job_t *bb_job = NULL;
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&bb_state.bb_mutex);
	if (job_ptr)
		bb_job = _get_bb_job(job_ptr);
	if (bb_job) {
		if (bb_job->state == BB_STATE_RUNNING)
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_STAGED_IN);
		else if (bb_job->state == BB_STATE_PRE_RUN)
			bb_set_job_bb_state(job_ptr, bb_job,
					    BB_STATE_ALLOC_REVOKE);
	} else {
		rc = SLURM_ERROR;
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/*
 * Trigger a job's burst buffer stage-out to begin
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_start_stage_out(job_record_t *job_ptr)
{
	int rc = SLURM_SUCCESS;
	bb_job_t *bb_job;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return SLURM_SUCCESS;

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "%pJ", job_ptr);

	if (bb_state.last_load_time == 0) {
		info("Burst buffer down, can not stage out %pJ",
		      job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	bb_job = _get_bb_job(job_ptr);
	if (!bb_job) {
		/* No job buffers. */
		error("%pJ bb job record not found", job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	} else if (bb_job->state < BB_STATE_RUNNING) {
		/* Job never started. Just teardown the buffer */
		bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_TEARDOWN);
		_queue_teardown(job_ptr->job_id, job_ptr->user_id, true,
				job_ptr->group_id);
	} else if (bb_job->state < BB_STATE_POST_RUN) {
		_pre_queue_stage_out(job_ptr, bb_job);
	}

fini:
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/*
 * Determine if a job's burst buffer post_run operation is complete
 *
 * RET: 0 - post_run is underway
 *      1 - post_run complete
 *     -1 - fatal error
 */
extern int bb_p_job_test_post_run(job_record_t *job_ptr)
{
	bb_job_t *bb_job;
	int rc = -1;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return 1;

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "%pJ", job_ptr);

	if (bb_state.last_load_time == 0) {
		info("Burst buffer down, can not post_run %pJ",
		      job_ptr);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return -1;
	}
	bb_job = bb_job_find(&bb_state, job_ptr->job_id);
	if (!bb_job) {
		error("%pJ bb job record not found, assuming post run is complete",
		      job_ptr);
		rc =  1;
	} else {
		if (bb_job->state < BB_STATE_POST_RUN) {
			rc = -1;
		} else if (bb_job->state > BB_STATE_POST_RUN) {
			rc =  1;
		} else {
			rc =  0;
		}
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/*
 * Determine if a job's burst buffer stage-out is complete
 *
 * RET: 0 - stage-out is underway
 *      1 - stage-out complete
 *     -1 - fatal error
 */
extern int bb_p_job_test_stage_out(job_record_t *job_ptr)
{
	bb_job_t *bb_job;
	int rc = -1;

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0'))
		return 1;

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "%pJ", job_ptr);

	if (bb_state.last_load_time == 0) {
		info("Burst buffer down, can not stage-out %pJ", job_ptr);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return -1;
	}
	bb_job = bb_job_find(&bb_state, job_ptr->job_id);
	if (!bb_job) {
		/* This is expected if the burst buffer completed teardown */
		rc = 1;
	} else {
		/*
		 * bb_g_job_test_stage_out() is called when purging old jobs
		 * from slurmctld and when testing for dependencies.
		 * We don't want the job to be purged until teardown is done
		 * (teardown happens right after stage_out). Once teardown is
		 * done the state will be BB_STATE_COMPLETE. We also free
		 * bb_job so it doesn't stay around forever.
		 */
		if (bb_job->state == BB_STATE_PENDING) {
			/*
			 * No job BB work started before job was killed.
			 * Alternately slurmctld daemon restarted after the
			 * job's BB work was completed.
			 */
			rc =  1;
		} else if (bb_job->state < BB_STATE_POST_RUN) {
			rc = -1;
		} else if (bb_job->state == BB_STATE_COMPLETE) {
			bb_job_del(&bb_state, bb_job->job_id);
			rc =  1;
		} else {
			rc =  0;
		}
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/*
 * Terminate any file staging and completely release burst buffer resources
 *
 * Returns a Slurm errno.
 */
extern int bb_p_job_cancel(job_record_t *job_ptr)
{
	bb_job_t *bb_job;

	slurm_mutex_lock(&bb_state.bb_mutex);
	log_flag(BURST_BUF, "%pJ", job_ptr);

	if (bb_state.last_load_time == 0) {
		info("Burst buffer down, can not cancel %pJ",
		      job_ptr);
		slurm_mutex_unlock(&bb_state.bb_mutex);
		return SLURM_ERROR;
	}

	bb_job = bb_job_find(&bb_state, job_ptr->job_id);
	if (!bb_job) {
		/* Nothing ever allocated, nothing to clean up */
	} else if (bb_job->state == BB_STATE_PENDING) {
		bb_set_job_bb_state(job_ptr, bb_job, /* Nothing to clean up */
				    BB_STATE_COMPLETE);
	} else if (bb_job->state == BB_STATE_COMPLETE) {
		/* Teardown already done. */
	} else {
		bb_set_job_bb_state(job_ptr, bb_job, BB_STATE_TEARDOWN);
		_queue_teardown(job_ptr->job_id, job_ptr->user_id, true,
				job_ptr->group_id);
	}
	slurm_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Run a script in the burst buffer plugin
 *
 * func IN - script function to run
 * jobid IN - job id for which we are running the script (0 if not for a job)
 * argc IN - number of arguments to pass to script
 * argv IN - argument list to pass to script
 * resp_msg OUT - string returned by script
 *
 * Returns the status of the script.
 */
extern int bb_p_run_script(char *func, uint32_t job_id, uint32_t argc,
			   char **argv, job_info_msg_t *job_info,
			   char **resp_msg)
{
	return _start_lua_script(func, job_id, argc, argv, job_info, resp_msg);
}

/*
 * Translate a burst buffer string to it's equivalent TRES string
 * For example:
 *   "bb/lua=2M" -> "1004=2"
 * Caller must xfree the return value
 */
extern char *bb_p_xlate_bb_2_tres_str(char *burst_buffer)
{
	char *save_ptr = NULL, *sep, *tmp, *tok;
	char *result = NULL;
	uint64_t size, total = 0;

	if (!burst_buffer || (bb_state.tres_id < 1))
		return result;

	tmp = xstrdup(burst_buffer);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		sep = strchr(tok, ':');
		if (sep) {
			if (!xstrncmp(tok, "lua:", 4))
				tok += 4;
			else
				tok = NULL;
		}

		if (tok) {
			uint64_t mb_xlate = 1024 * 1024;
			size = bb_get_size_num(tok,
					       bb_state.bb_config.granularity);
			total += (size + mb_xlate - 1) / mb_xlate;
		}

		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);

	if (total)
		xstrfmtcat(result, "%d=%"PRIu64, bb_state.tres_id, total);

	return result;
}
