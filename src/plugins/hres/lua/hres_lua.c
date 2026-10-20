/*****************************************************************************\
 *  hres_lua.c - Hierarchical resource plugin backed by a lua script
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#define _GNU_SOURCE

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "slurm/slurm.h"

#include "src/common/job_record.h"
#include "src/common/log.h"
#include "src/common/sluid.h"
#include "src/common/threadpool.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/hres.h"

#include "src/lua/slurm_lua.h"

#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

/*
 * These variables are required by the generic plugin interface. See slurm.h.
 */
const uint32_t plugin_id = HRES_PLUGIN_LUA;
const char plugin_name[] = "hres lua plugin";
const char plugin_type[] = "hres/lua";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/*
 * Functions the script must define. hres_controller_job_async_prerun is
 * optional, only the state built to run it requires it.
 */
/* clang-format off */
static const char *async_req_fxns[] = {
	"hres_controller_job_async_prerun",
	NULL
};

static const char *req_fxns[] = {
	"hres_create_context",
	"hres_destroy_context",
	"hres_maintenance",
	"hres_controller_job_prerun",
	"hres_controller_job_revoke",
	"hres_controller_job_epilog",
	"hres_node_job_prolog",
	"hres_node_job_epilog",
	"hres_node_task_prolog",
	"hres_node_task_epilog",
	"hres_node_user_task_prolog",
	NULL
};
/* clang-format on */

/*
 * Per-context lua state, owned through hres_ctx_t->plugin_ctx. Node callbacks
 * build a transient one per call instead.
 */
typedef struct {
	lua_State *L;
	time_t last_loaded; /* mtime of the loaded script */
	pthread_mutex_t lock; /* guards L, callers hold different locks */
	lua_State *maint_L;
	time_t maint_last_loaded; /* mtime of the script loaded in maint_L */
	char *script_path;
} hres_lua_ctx_t;

/*
 * Arguments for one async pre-run thread, owned and freed by it. L is an
 * independent state with the function and its arguments already pushed.
 */
typedef struct {
	bool cancelled; /* revoked, the result is dropped */
	uint16_t hres_id; /* HRES whose prep the result is stored on */
	lua_State *L; /* pre-loaded async state, closed by the thread */
	slurm_step_id_t step_id; /* job to re-find, sluid detects a requeue */
} async_prerun_t;

/*
 * Detached async pre-run threads still running, so fini() can wait for them
 * before closing lua. async_lock is never held across a lua call or a
 * slurmctld lock. Arming is never refused: the sync pre-run has already run,
 * so skipping the async half would launch the job half configured.
 * async_list holds the threads whose result is not applied yet, so a revoke
 * can cancel them.
 */
static int async_running = 0;
static list_t *async_list = NULL;
static pthread_cond_t async_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t async_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	int idx; /* 1-based lua array index of the next layer */
	lua_State *st;
} foreach_push_layer_t;

/* Push a "{name = value, ...}" table built from a list of hres_config_t. */
static int _foreach_push_config(void *x, void *arg)
{
	hres_config_t *config = x;
	lua_State *st = arg;

	lua_pushstring(st, config->value);
	lua_setfield(st, -2, config->name);

	return 0;
}

static void _push_config(lua_State *L, list_t *config)
{
	lua_newtable(L);
	if (config)
		list_for_each_ro(config, _foreach_push_config, L);
}

/* Push one layer as "{count = N, name = "...", nodes = "..."}". */
static int _foreach_push_layer(void *x, void *arg)
{
	hres_prep_layer_t *layer = x;
	foreach_push_layer_t *args = arg;

	lua_newtable(args->st);
	lua_pushinteger(args->st, layer->count);
	lua_setfield(args->st, -2, "count");
	lua_pushstring(args->st, layer->layer_name);
	lua_setfield(args->st, -2, "name");
	lua_pushstring(args->st, layer->nodes);
	lua_setfield(args->st, -2, "nodes");
	lua_rawseti(args->st, -2, args->idx++);

	return 0;
}

static void _push_layers(lua_State *L, list_t *layers)
{
	foreach_push_layer_t args = {
		.idx = 1,
		.st = L,
	};

	lua_newtable(L);
	if (layers)
		list_for_each_ro(layers, _foreach_push_layer, &args);
}

/* Push one cluster layer as {disable_layer, name, nodes, total, used}. */
static int _foreach_push_hres_layer(void *x, void *arg)
{
	hres_layer_t *layer = x;
	foreach_push_layer_t *args = arg;

	lua_newtable(args->st);
	lua_pushboolean(args->st, layer->disable_layer);
	lua_setfield(args->st, -2, "disable_layer");
	lua_pushstring(args->st, layer->name);
	lua_setfield(args->st, -2, "name");
	lua_pushstring(args->st, layer->nodes);
	lua_setfield(args->st, -2, "nodes");
	lua_pushinteger(args->st, layer->total);
	lua_setfield(args->st, -2, "total");
	lua_pushinteger(args->st, layer->used);
	lua_setfield(args->st, -2, "used");
	lua_rawseti(args->st, -2, args->idx++);

	return 0;
}

static void _push_hres_layers(lua_State *L, list_t *layers)
{
	foreach_push_layer_t args = {
		.idx = 1,
		.st = L,
	};

	lua_newtable(L);
	if (layers)
		list_for_each_ro(layers, _foreach_push_hres_layer, &args);
}

/* Copy down and disable_layer back from the layer table at the stack top. */
static int _foreach_read_hres_layer(void *x, void *arg)
{
	hres_layer_t *layer = x;
	foreach_push_layer_t *args = arg;

	lua_rawgeti(args->st, -1, args->idx++);
	if (lua_istable(args->st, -1)) {
		lua_getfield(args->st, -1, "down");
		layer->down = lua_toboolean(args->st, -1);
		lua_getfield(args->st, -2, "disable_layer");
		layer->disable_layer = lua_toboolean(args->st, -1);
		lua_pop(args->st, 2);
	}
	lua_pop(args->st, 1);

	return 0;
}

/*
 * Push what a node callback receives about the resource, as a single table:
 *	{ name, payload, config = {...}, plugin_args = {...},
 *	  layers = {{count, name, nodes}, ...} }
 */
static void _push_prep(lua_State *L, hres_prep_t *prep)
{
	lua_newtable(L);

	lua_pushstring(L, prep->name);
	lua_setfield(L, -2, "name");

	/* Opaque bytes from the pre-run, if any. */
	if (prep->payload) {
		lua_pushlstring(L, prep->payload, prep->payload_size);
		lua_setfield(L, -2, "payload");
	}

	_push_config(L, prep->config);
	lua_setfield(L, -2, "config");
	_push_config(L, prep->plugin_args);
	lua_setfield(L, -2, "plugin_args");
	_push_layers(L, prep->layers);
	lua_setfield(L, -2, "layers");
}

/* Push the job identity: { job_id, sluid, user_id, nodes }. */
static void _push_job(lua_State *L, hres_job_t *job)
{
	char sluid[SLUID_STR_BYTES];

	lua_newtable(L);

	lua_pushinteger(L, job->step_id.job_id);
	lua_setfield(L, -2, "job_id");
	print_sluid(job->step_id.sluid, sluid, sizeof(sluid));
	lua_pushstring(L, sluid);
	lua_setfield(L, -2, "sluid");
	lua_pushinteger(L, job->user_id);
	lua_setfield(L, -2, "user_id");
	lua_pushstring(L, job->nodes);
	lua_setfield(L, -2, "nodes");
}

/* Push what a context callback receives: { name, config = {...} }. */
static void _push_ctx(lua_State *L, hres_ctx_t *ctx)
{
	lua_newtable(L);

	lua_pushstring(L, ctx->name);
	lua_setfield(L, -2, "name");
	_push_config(L, ctx->config);
	lua_setfield(L, -2, "config");
}

/*
 * (Re)load script_path into *L and push the named function. Returns false with
 * a clean stack when the script or the function is absent.
 *
 * Callers needing their own state pass a NULL *L and a zeroed last_loaded on
 * every call: slurm_lua_loadscript() keys off the script mtime alone, so a
 * retained mtime skips the load and leaves *L NULL. Costs one compile a call.
 */
static bool _start_call(lua_State **L, const char *script_path,
			time_t *last_loaded, const char **fxns,
			const char *func)
{
	if (!script_path)
		return false;

	if (slurm_lua_loadscript(L, "hres/lua", script_path, fxns, last_loaded,
				 NULL, NULL))
		return false;

	lua_getglobal(*L, func);
	if (lua_isnil(*L, -1)) {
		lua_pop(*L, 1);
		return false;
	}

	return true;
}

/*
 * Call the function with nargs already on the stack and return its numeric
 * result as a Slurm rc. A non-numeric or missing result is treated as success.
 */
static int _finish_call(lua_State *L, const char *func, int nargs,
			char **err_msg)
{
	int rc;
	char *err_str = NULL;

	if ((rc = slurm_lua_pcall(L, nargs, 1, &err_str, func))) {
		error("hres/lua: %s: %s", func,
		      err_str ? err_str : slurm_strerror(rc));
		if (err_msg)
			SWAP(*err_msg, err_str);
		xfree(err_str);
		return rc;
	}

	if (lua_isnumber(L, -1))
		rc = lua_tonumber(L, -1);
	else
		rc = SLURM_SUCCESS;
	lua_pop(L, 1);

	return rc;
}

/*
 * _finish_call() for the pre-run: a second return value, a byte string,
 * becomes the payload sent to the nodes. Anything else leaves no payload.
 */
static int _finish_prerun(lua_State *L, hres_prep_t *prep, char **err_msg)
{
	int rc;
	char *err_str = NULL;

	if ((rc = slurm_lua_pcall(L, 3, 2, &err_str,
				  "hres_controller_job_prerun"))) {
		error("hres/lua: hres_controller_job_prerun: %s",
		      err_str ? err_str : slurm_strerror(rc));
		if (err_msg)
			SWAP(*err_msg, err_str);
		xfree(err_str);
		return rc;
	}

	/* Stack: rc at -2, payload at -1. */
	if (lua_isstring(L, -1)) {
		size_t len;
		const char *payload = lua_tolstring(L, -1, &len);

		if (len) {
			prep->payload = xmalloc(len);
			memcpy(prep->payload, payload, len);
			prep->payload_size = len;
		}
	}

	rc = lua_isnumber(L, -2) ? lua_tonumber(L, -2) : SLURM_SUCCESS;
	lua_pop(L, 2);

	return rc;
}

/* Run a node callback in a transient state built from the credential. */
static int _node_call(const char *func, hres_prep_t *prep, hres_job_t *job,
		      uint32_t node_tid, uint32_t global_tid, int nargs)
{
	int rc = SLURM_SUCCESS;
	lua_State *L = NULL;
	time_t last_loaded = 0;

	if (_start_call(&L, hres_config_value(prep->config, "path"),
			&last_loaded, req_fxns, func)) {
		_push_prep(L, prep);
		_push_job(L, job);
		if (nargs == 4) {
			lua_pushinteger(L, node_tid);
			lua_pushinteger(L, global_tid);
		}
		rc = _finish_call(L, func, nargs, NULL);
	} else {
		rc = SLURM_ERROR;
	}

	if (L)
		lua_close(L);

	return rc;
}

extern int hres_p_create_context(hres_ctx_t *ctx, list_t *layers)
{
	hres_lua_ctx_t *lua_ctx = xmalloc(sizeof(*lua_ctx));

	slurm_mutex_init(&lua_ctx->lock);
	lua_ctx->script_path = xstrdup(hres_config_value(ctx->config, "path"));
	ctx->plugin_ctx = lua_ctx;

	if (!lua_ctx->script_path) {
		error("hres/lua: HRES %s has no 'path' in its plugin config",
		      ctx->name);
		return SLURM_ERROR;
	}

	if (_start_call(&lua_ctx->L, lua_ctx->script_path,
			&lua_ctx->last_loaded, req_fxns,
			"hres_create_context")) {
		_push_ctx(lua_ctx->L, ctx);
		_push_hres_layers(lua_ctx->L, layers);
		return _finish_call(lua_ctx->L, "hres_create_context", 2, NULL);
	}

	return SLURM_ERROR;
}

extern int hres_p_destroy_context(hres_ctx_t *ctx)
{
	hres_lua_ctx_t *lua_ctx = ctx->plugin_ctx;
	int rc = SLURM_SUCCESS;

	if (!lua_ctx)
		return SLURM_SUCCESS;

	if (_start_call(&lua_ctx->L, lua_ctx->script_path,
			&lua_ctx->last_loaded, req_fxns,
			"hres_destroy_context")) {
		_push_ctx(lua_ctx->L, ctx);
		rc = _finish_call(lua_ctx->L, "hres_destroy_context", 1, NULL);
	}

	if (lua_ctx->L)
		lua_close(lua_ctx->L);
	if (lua_ctx->maint_L)
		lua_close(lua_ctx->maint_L);
	slurm_mutex_destroy(&lua_ctx->lock);
	xfree(lua_ctx->script_path);
	xfree(lua_ctx);
	ctx->plugin_ctx = NULL;

	return rc;
}

extern int hres_p_maintenance(hres_ctx_t *ctx, list_t *layers)
{
	hres_lua_ctx_t *lua_ctx = ctx->plugin_ctx;
	int rc = SLURM_SUCCESS;

	if (!lua_ctx)
		return SLURM_SUCCESS;

	if (_start_call(&lua_ctx->maint_L, lua_ctx->script_path,
			&lua_ctx->maint_last_loaded, req_fxns,
			"hres_maintenance")) {
		foreach_push_layer_t args = {
			.idx = 1,
			.st = lua_ctx->maint_L,
		};

		_push_ctx(lua_ctx->maint_L, ctx);
		_push_hres_layers(lua_ctx->maint_L, layers);
		/* Keep the layers table below the function to read it back. */
		lua_pushvalue(lua_ctx->maint_L, -1);
		lua_insert(lua_ctx->maint_L, -4);
		rc = _finish_call(lua_ctx->maint_L, "hres_maintenance", 2,
				  NULL);
		if (layers)
			list_for_each_ro(layers, _foreach_read_hres_layer,
					 &args);
		lua_pop(lua_ctx->maint_L, 1);
	}

	return rc;
}

static int _find_prep_by_hres_id(void *x, void *key)
{
	hres_prep_t *prep = x;
	uint16_t *hres_id = key;

	return (prep->hres_id == *hres_id);
}

/*
 * Fail a job whose async pre-run failed. The job already holds its allocation,
 * so requeue it like a failed PrologSlurmctld and kill it if that is refused.
 * Act on the leader for a het job, a component cannot be requeued alone.
 */
static void _fail_prerun_job(job_record_t *job_ptr)
{
	job_record_t *target = job_ptr;
	int rc = SLURM_SUCCESS;

	if (job_ptr->het_job_id)
		target = find_job_record(job_ptr->het_job_id);

	if (target)
		rc = job_requeue_internal(0, target, false, 0);

	if (target && (!rc || (rc == ESLURM_JOB_PENDING)))
		return;

	info("hres/lua: unable to requeue %pJ: %s", job_ptr,
	     target ? slurm_strerror(rc) : "no het job leader");

	if (!job_ptr->het_job_id)
		job_signal(NULL, job_ptr, SIGKILL, 0, false);
	else if (target)
		(void) het_job_signal(NULL, target, SIGKILL, 0, false);
	else
		error("hres/lua: no het job leader for %pJ", job_ptr);
}

/*
 * Run the async pre-run, then under the job write lock store its payload and
 * release the launch deferral.
 */
static void *_async_prerun_thread(void *x)
{
	async_prerun_t *args = x;
	slurmctld_lock_t job_write_lock = {
		.job = WRITE_LOCK,
		.node = WRITE_LOCK,
		.fed = READ_LOCK,
	};
	int rc;
	char *err_str = NULL;
	char *payload = NULL;
	size_t payload_size = 0;
	job_record_t *job_ptr;
	hres_prep_t *prep;
	bool cancelled;

	/* Unlocked, the state belongs to this thread. */
	if (!args->L) /* Failed to load, logged by slurm_lua_loadscript(). */
		rc = SLURM_ERROR;
	else
		rc = slurm_lua_pcall(args->L, 3, 2, &err_str,
				     "hres_controller_job_async_prerun");
	/* Already logged by slurm_lua_pcall(). */
	xfree(err_str);
	if (!rc && lua_isstring(args->L, -1)) {
		size_t len;
		const char *buf = lua_tolstring(args->L, -1, &len);

		if (len) {
			payload = xmalloc(len);
			memcpy(payload, buf, len);
			payload_size = len;
		}
	}
	if (!rc)
		rc = lua_isnumber(args->L, -2) ? lua_tonumber(args->L, -2) :
						 SLURM_SUCCESS;

	lock_slurmctld(job_write_lock);

	/* A revoke runs under the job write lock too, it cannot race this. */
	slurm_mutex_lock(&async_lock);
	cancelled = args->cancelled;
	list_delete_ptr(async_list, args);
	slurm_mutex_unlock(&async_lock);

	/* By job_id, a requeue gives the job a new sluid. */
	if ((job_ptr = find_job_record(args->step_id.job_id))) {
		/* Revoked, requeued or ended meanwhile, the result is stale. */
		bool stale = cancelled || IS_JOB_COMPLETED(job_ptr) ||
			     (job_ptr->step_id.sluid != args->step_id.sluid);

		if (!stale && !rc && payload &&
		    (prep = list_find_first(job_ptr->hres_prep,
					    _find_prep_by_hres_id,
					    &args->hres_id))) {
			xfree(prep->payload);
			prep->payload = payload;
			prep->payload_size = payload_size;
			payload = NULL;
		}
		if (!stale && rc) {
			error("hres/lua: async pre-run failed for %pJ: %s",
			      job_ptr, slurm_strerror(rc));
			_fail_prerun_job(job_ptr);
		}
		prolog_running_decr(job_ptr);
	}
	unlock_slurmctld(job_write_lock);

	xfree(payload);
	if (args->L)
		lua_close(args->L);
	xfree(args);

	/* Last, fini() waits on this before closing lua. */
	slurm_mutex_lock(&async_lock);
	async_running--;
	slurm_cond_broadcast(&async_cond);
	slurm_mutex_unlock(&async_lock);

	return NULL;
}

extern int hres_p_controller_job_prerun(job_record_t *job_ptr, hres_ctx_t *ctx,
					hres_prep_t *prep, char **err_msg)
{
	hres_lua_ctx_t *lua_ctx = ctx->plugin_ctx;
	hres_job_t job = {
		.nodes = job_ptr->nodes,
		.step_id = STEP_ID_FROM_JOB_RECORD(job_ptr),
		.user_id = job_ptr->user_id,
	};
	lua_State *async_L = NULL;
	time_t async_last_loaded = 0;
	int rc = SLURM_SUCCESS;
	bool async;
	async_prerun_t *args;

	if (!lua_ctx)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&lua_ctx->lock);
	if (_start_call(&lua_ctx->L, lua_ctx->script_path,
			&lua_ctx->last_loaded, req_fxns,
			"hres_controller_job_prerun")) {
		_push_ctx(lua_ctx->L, ctx);
		_push_job(lua_ctx->L, &job);
		_push_prep(lua_ctx->L, prep);
		rc = _finish_prerun(lua_ctx->L, prep, err_msg);
	}
	/* Only build a second state when the script has an async pre-run. */
	async = lua_ctx->L &&
		slurm_lua_is_function_defined(
			lua_ctx->L, "hres_controller_job_async_prerun");
	slurm_mutex_unlock(&lua_ctx->lock);

	if (rc || !async)
		return rc;

	/*
	 * Push the arguments while ctx/job/prep are still valid and hand the
	 * state to a detached thread, which frees it and args. Without a state
	 * the thread fails the async pre-run.
	 */
	if (_start_call(&async_L, lua_ctx->script_path, &async_last_loaded,
			async_req_fxns, "hres_controller_job_async_prerun")) {
		_push_ctx(async_L, ctx);
		_push_job(async_L, &job);
		_push_prep(async_L, prep);
	}

	args = xmalloc(sizeof(*args));
	args->hres_id = prep->hres_id;
	args->step_id = job.step_id;
	args->L = async_L;

	/* Count before arming so fini() cannot miss a new thread. */
	slurm_mutex_lock(&async_lock);
	async_running++;
	list_append(async_list, args);

	/* Defer launch until the thread completes. */
	job_ptr->details->prolog_running++;
	job_state_set_flag(job_ptr, JOB_CONFIGURING);

	slurm_thread_create_detached(NULL, _async_prerun_thread, args);
	slurm_mutex_unlock(&async_lock);

	return rc;
}

static int _find_async_prerun(void *x, void *key)
{
	async_prerun_t *args = x;
	async_prerun_t *target = key;

	return ((args->hres_id == target->hres_id) &&
		(args->step_id.job_id == target->step_id.job_id) &&
		(args->step_id.sluid == target->step_id.sluid));
}

extern int hres_p_controller_job_revoke(job_record_t *job_ptr, hres_ctx_t *ctx,
					hres_prep_t *prep)
{
	hres_lua_ctx_t *lua_ctx = ctx->plugin_ctx;
	hres_job_t job = {
		.nodes = job_ptr->nodes,
		.step_id = STEP_ID_FROM_JOB_RECORD(job_ptr),
		.user_id = job_ptr->user_id,
	};
	async_prerun_t key = {
		.hres_id = prep->hres_id,
		.step_id = job.step_id,
	};
	async_prerun_t *args;
	int rc = SLURM_SUCCESS;

	/*
	 * Drop the async pre-run's result. Its thread keeps prolog_running
	 * until it ends, so the job is not scheduled again before that.
	 */
	slurm_mutex_lock(&async_lock);
	if ((args = list_find_first(async_list, _find_async_prerun, &key))) {
		args->cancelled = true;
		job_state_unset_flag(job_ptr, JOB_CONFIGURING);
	}
	slurm_mutex_unlock(&async_lock);

	if (!lua_ctx)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&lua_ctx->lock);
	if (_start_call(&lua_ctx->L, lua_ctx->script_path,
			&lua_ctx->last_loaded, req_fxns,
			"hres_controller_job_revoke")) {
		_push_ctx(lua_ctx->L, ctx);
		_push_job(lua_ctx->L, &job);
		_push_prep(lua_ctx->L, prep);
		rc = _finish_call(lua_ctx->L, "hres_controller_job_revoke", 3,
				  NULL);
	}
	slurm_mutex_unlock(&lua_ctx->lock);

	return rc;
}

extern int hres_p_controller_job_epilog(job_record_t *job_ptr, hres_ctx_t *ctx,
					hres_prep_t *prep)
{
	hres_lua_ctx_t *lua_ctx = ctx->plugin_ctx;
	hres_job_t job = {
		.nodes = job_ptr->nodes,
		.step_id = STEP_ID_FROM_JOB_RECORD(job_ptr),
		.user_id = job_ptr->user_id,
	};
	int rc = SLURM_SUCCESS;

	if (!lua_ctx)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&lua_ctx->lock);
	if (_start_call(&lua_ctx->L, lua_ctx->script_path,
			&lua_ctx->last_loaded, req_fxns,
			"hres_controller_job_epilog")) {
		_push_ctx(lua_ctx->L, ctx);
		_push_job(lua_ctx->L, &job);
		_push_prep(lua_ctx->L, prep);
		rc = _finish_call(lua_ctx->L, "hres_controller_job_epilog", 3,
				  NULL);
	}
	slurm_mutex_unlock(&lua_ctx->lock);

	return rc;
}

extern int hres_p_node_job_prolog(hres_prep_t *prep, hres_job_t *job)
{
	return _node_call("hres_node_job_prolog", prep, job, 0, 0, 2);
}

extern int hres_p_node_job_epilog(hres_prep_t *prep, hres_job_t *job)
{
	return _node_call("hres_node_job_epilog", prep, job, 0, 0, 2);
}

extern int hres_p_node_task_prolog(hres_prep_t *prep, hres_job_t *job,
				   uint32_t node_tid, uint32_t global_tid)
{
	return _node_call("hres_node_task_prolog", prep, job, node_tid,
			  global_tid, 4);
}

extern int hres_p_node_task_epilog(hres_prep_t *prep, hres_job_t *job,
				   uint32_t node_tid, uint32_t global_tid)
{
	return _node_call("hres_node_task_epilog", prep, job, node_tid,
			  global_tid, 4);
}

extern int hres_p_node_user_task_prolog(hres_prep_t *prep, hres_job_t *job,
					uint32_t node_tid, uint32_t global_tid)
{
	return _node_call("hres_node_user_task_prolog", prep, job, node_tid,
			  global_tid, 4);
}

extern int init(void)
{
	async_list = list_create(NULL);

	/* Only dlopen liblua, states are built lazily per context. */
	return slurm_lua_init();
}

/*
 * A script blocked in a network call delays this, it cannot be interrupted in
 * process.
 */
static void _wait_async(void)
{
	slurm_mutex_lock(&async_lock);
	while (async_running)
		slurm_cond_wait(&async_cond, &async_lock);
	slurm_mutex_unlock(&async_lock);
}

extern int hres_p_controller_stop(void)
{
	/* The async pre-runs store their result under the job write lock. */
	_wait_async();

	return SLURM_SUCCESS;
}

extern void fini(void)
{
	/* Threads close their own state, before slurm_lua_fini(). */
	_wait_async();
	FREE_NULL_LIST(async_list);

	slurm_lua_fini();
}
