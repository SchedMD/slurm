/*****************************************************************************\
 *  runtime_lua.c - lua job runtime plugin.
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

#include "config.h"

#include "slurm/slurm.h"

#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"

#include "src/interfaces/runtime.h"

#include "src/lua/slurm_lua.h"

/*
 * These variables are required by the generic plugin interface.  See plugin.h
 * for more information.
 */
const char plugin_name[] = "lua runtime plugin";
const char plugin_type[] = "runtime/lua";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static char *lua_script_path = NULL;

/* Functions that runtime.lua must provide */
static const char *req_fxns[] = {
	"slurm_runtime_setup",
	"slurm_runtime_cleanup",
	"slurm_runtime_task_init",
	"slurm_runtime_run",
	NULL,
};

/* Pushes step id as string and numeric task id or NIL */
static int _push_step_id(lua_State *L, stepd_step_rec_t *step,
			 stepd_step_task_info_t *task)
{
	slurm_selected_step_t id = SLURM_SELECTED_STEP_INITIALIZER;
	char *id_str = NULL;
	int rc;

	id.step_id = step->step_id;
	id.array_task_id = step->array_task_id;

	/* fmt_job_id_string() appends the suffix to the master id, not ours. */
	if (step->array_task_id != NO_VAL) {
		id.step_id.job_id = step->array_job_id;
	} else if (step->het_job_id && (step->het_job_id != NO_VAL)) {
		id.het_job_offset = step->het_job_offset;
		id.step_id.job_id = step->het_job_id;
	}

	if (!(rc = fmt_job_id_string(&id, &id_str)))
		lua_pushstring(L, id_str);

	xfree(id_str);

	if (task)
		lua_pushinteger(L, task->id);
	else
		lua_pushnil(L);

	return rc;
}

/*
 * Call fxn in the script.
 * IN fxn - name of the function to call, which must be in req_fxns
 * IN step - step record pushed as the first argument
 * IN task - task record pushed as the second argument, or NULL for none
 * OUT script_rc - value the script returned, only set on SLURM_SUCCESS
 * RET SLURM_SUCCESS if the script ran, or an error if it could not be called
 *
 * The lua state is created here and closed again before returning. Nothing is
 * shared between calls, so callers in separate threads cannot corrupt each
 * other's state and no locking is needed. The cost is that the script is
 * re-read and re-compiled every time, which is the same trade burst_buffer/lua
 * makes for the same reason.
 */
static int _call_lua(const char *fxn, stepd_step_rec_t *step,
		     stepd_step_task_info_t *task, int *script_rc)
{
	int rc = EINVAL;
	char *err = NULL;
	lua_State *L = NULL;
	time_t load_time = 0;

	if ((rc = slurm_lua_loadscript(&L, plugin_type, lua_script_path,
				       req_fxns, &load_time, NULL, NULL)))
		return rc;

	lua_getglobal(L, fxn);
	if (lua_isnil(L, -1)) {
		error("%s: %s: %s is not defined", plugin_type, lua_script_path,
		      fxn);
		rc = ESLURM_LUA_FUNC_NOT_FOUND;
		goto cleanup;
	}

	if ((rc = _push_step_id(L, step, task)))
		goto cleanup;

	slurm_lua_stack_dump(plugin_type, "before lua_pcall", L);

	if ((rc = slurm_lua_pcall(L, 2, 1, &err, __func__))) {
		error("%s: %s: %s failed: %s", plugin_type, lua_script_path,
		      fxn, err);
		xfree(err);
		lua_close(L);
		return rc;
	}

	slurm_lua_stack_dump(plugin_type, "after lua_pcall", L);

	if (lua_isnumber(L, -1)) {
		*script_rc = lua_tonumber(L, -1);
	} else {
		error("%s: %s: %s returned a non-numeric value", plugin_type,
		      lua_script_path, fxn);
		rc = SLURM_ERROR;
		goto cleanup;
	}
	lua_pop(L, 1);

cleanup:
	lua_close(L);
	return rc;
}

extern int runtime_p_init(runtime_context_t context)
{
	int rc = SLURM_SUCCESS;
	lua_State *L = NULL;
	time_t load_time = (time_t) 0;

	if ((rc = slurm_lua_init()))
		return rc;

	xfree(lua_script_path);
	lua_script_path = get_extra_conf_path("runtime.lua");

	/*
	 * Load the script once here so that a missing or broken runtime.lua is
	 * reported when the plugin is loaded rather than mid-step. The state is
	 * closed again: each call builds its own.
	 */
	if (!(rc = slurm_lua_loadscript(&L, plugin_type, lua_script_path,
					req_fxns, &load_time, NULL, NULL)))
		lua_close(L);

	return rc;
}

extern int runtime_p_fini(void)
{
	xfree(lua_script_path);

	slurm_lua_fini();

	return SLURM_SUCCESS;
}

extern int runtime_p_setup(slurmd_conf_t *conf, stepd_step_rec_t *step,
			   slurm_addr_t *cli, slurm_msg_t *msg)
{
	int rc = EINVAL, script_rc = EINVAL;

	if (!(rc = _call_lua("slurm_runtime_setup", step, NULL, &script_rc)))
		rc = script_rc;

	return rc;
}

extern void runtime_p_cleanup(slurmd_conf_t *conf, stepd_step_rec_t *step)
{
	int rc = EINVAL, script_rc = EINVAL;

	if (!(rc = _call_lua("slurm_runtime_cleanup", step, NULL, &script_rc)))
		rc = script_rc;

	/* The interface has no way to report this to the caller. */
	if (rc)
		error("%s: %s: slurm_runtime_cleanup() failed: %s", plugin_type,
		      lua_script_path, slurm_strerror(rc));
}

extern void runtime_p_task_init(slurmd_conf_t *conf, stepd_step_rec_t *step,
				stepd_step_task_info_t *task)
{
	int rc = EINVAL, script_rc = EINVAL;

	/*
	 * This runs in the task child after fork() and after privileges have
	 * been dropped, so runtime.lua is read here as the job user and must
	 * be readable by them.
	 */
	if (!(rc = _call_lua("slurm_runtime_task_init", step, task,
			     &script_rc)))
		rc = script_rc;

	/* The interface has no way to report this to the caller. */
	if (rc)
		error("%s: %s: slurm_runtime_task_init() failed: %s",
		      plugin_type, lua_script_path, slurm_strerror(rc));
}

extern int runtime_p_run(slurmd_conf_t *conf, stepd_step_rec_t *step,
			 stepd_step_task_info_t *task)
{
	int rc = EINVAL, script_rc = EINVAL;

	/* Runs in the task child as the job user, see runtime_p_task_init(). */
	if ((rc = _call_lua("slurm_runtime_run", step, task, &script_rc))) {
		/* Broken script: fail rather than exec the task. */
		return SLURM_ERROR;
	}

	/*
	 * A script that execs the task never returns here, so a successful
	 * return means it declined to run it and the caller has to.
	 * runtime_g_run() asserts that this never returns SLURM_SUCCESS, and
	 * treats anything else as the errno of a failed exec.
	 */
	if (script_rc == SLURM_SUCCESS)
		return ESLURM_NOT_SUPPORTED;

	return script_rc;
}
