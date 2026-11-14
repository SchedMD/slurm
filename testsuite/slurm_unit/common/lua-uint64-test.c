/*****************************************************************************\
 *  lua-uint64-test.c - Lua 64-bit sentinel registration and field tests.
 *****************************************************************************
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE. See the GNU General Public License for details.
\*****************************************************************************/

#include "config.h"
#include <stdio.h>
#include <stdlib.h>

/* Exercise the real descriptor accessors, including their Lua metatables. */
#include "src/plugins/job_submit/lua/job_submit_lua.c"

/* Unlike assert(), these checks also run with NDEBUG. */
#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, \
				#condition); \
			exit(EXIT_FAILURE); \
		} \
	} while (0)

extern buf_t *get_job_script(const job_record_t *job_ptr)
{
	/* Required by the shared job-record accessor; unused by these tests. */
	CHECK(false);
	return NULL;
}

static void _register_test_globals(lua_State *st)
{
	lua_setglobal(st, "slurm");
}

static void _run(const char *script)
{
	int rc = luaL_dostring(L, script);

	if (rc != LUA_OK)
		fprintf(stderr, "Lua test failed: %s\n", lua_tostring(L, -1));
	CHECK(rc == LUA_OK);
}

static void _test_constants(void)
{
	_run("assert(type(slurm.NO_VAL64) == 'number')\n"
	     "assert(type(slurm.INFINITE64) == 'number')\n"
	     "assert(slurm.NO_VAL64 == -2 and slurm.INFINITE64 == -1)\n"
	     "assert(slurm.NO_VAL64 ~= slurm.INFINITE64)\n"
	     "assert(slurm.NO_VAL64 ~= 0 and slurm.INFINITE64 ~= 0)\n"
	     "if math.type then\n"
	     " assert(math.type(slurm.NO_VAL64) == 'integer')\n"
	     " assert(math.type(slurm.INFINITE64) == 'integer')\n"
	     "end");
}

static void _test_fields(bool is_modify)
{
	const uint64_t values[] = { NO_VAL64, INFINITE64, 0, 1025 };
	const char *names[] = { "slurm.NO_VAL64", "slurm.INFINITE64", "0",
				"1025" };
	job_desc_msg_t desc;
	job_details_t details = { 0 };
	job_record_t job = { .details = &details };
	char script[512];

	slurm_init_job_desc_msg(&desc);
	CHECK(desc.pn_min_memory == NO_VAL64);
	_push_job_desc(&desc, is_modify);
	lua_setglobal(L, "job_desc");
	_run("assert(job_desc.pn_min_memory == slurm.NO_VAL64)\n"
	     "assert(job_desc.min_mem_per_node == nil and job_desc.min_mem_per_cpu == nil)");

	for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		desc.pn_min_memory = details.pn_min_memory = values[i];
		slurm_lua_job_record_field(L, &job, "pn_min_memory");
		lua_setglobal(L, "record_memory");
		snprintf(script, sizeof(script),
			 "assert(job_desc.pn_min_memory == %s)\n"
			 "assert(record_memory == %s)\n"
			 "job_desc.pn_min_memory = job_desc.pn_min_memory",
			 names[i], names[i]);
		_run(script);
		CHECK(desc.pn_min_memory == values[i]);
		/* Test assignment from the actual registered constants as well. */
		desc.pn_min_memory = 17;
		snprintf(script, sizeof(script), "job_desc.pn_min_memory = %s",
			 names[i]);
		_run(script);
		CHECK(desc.pn_min_memory == values[i]);
	}

	job.details = NULL;
	slurm_lua_job_record_field(L, &job, "pn_min_memory");
	lua_setglobal(L, "record_memory");
	_run("assert(record_memory == slurm.NO_VAL64)\n"
	     "job_desc.pn_min_memory = 0\n"
	     "assert(job_desc.pn_min_memory ~= slurm.NO_VAL64)\n"
	     "assert(job_desc.min_mem_per_node == 0 and job_desc.min_mem_per_cpu == nil)");
	CHECK(desc.pn_min_memory == 0);

	desc.pn_min_memory = 17;
	_run("local invalid = {-3, -math.huge, math.huge, 0/0, 2^64}\n"
	     "for _, value in ipairs(invalid) do\n"
	     " local ok = pcall(function() job_desc.pn_min_memory = value end)\n"
	     " assert(not ok)\n"
	     "end");
	CHECK(desc.pn_min_memory == 17);
}

int main(void)
{
	const char *required[] = { NULL };
	time_t load_time = 0;
	char *error_msg = NULL;

	/* The normal script loader must register the constants being tested. */
	CHECK(slurm_lua_loadscript(&L, "lua-uint64-test", LUA_TEST_SCRIPT,
				   required, &load_time, _register_test_globals,
				   &error_msg) == SLURM_SUCCESS);
	CHECK(L != NULL && error_msg == NULL);
	_test_constants();
	_test_fields(false);
	_test_fields(true);
	lua_close(L);
	L = NULL;
	puts("Lua uint64 sentinels: all tests passed");
	return EXIT_SUCCESS;
}
