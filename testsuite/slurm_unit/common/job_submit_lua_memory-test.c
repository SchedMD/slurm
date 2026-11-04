/*****************************************************************************\
 *  job_submit_lua_memory-test.c - Exact packed memory conversion tests.
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
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for details.
\*****************************************************************************/

#include "config.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test the actual descriptor accessors, including their Lua metatables. */
#include "src/plugins/job_submit/lua/job_submit_lua.c"

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
	/* The common Lua job-record accessor references this unrelated field. */
	CHECK(false);
	return NULL;
}

static void _run(const char *script)
{
	int rc = luaL_dostring(L, script);

	if (rc != LUA_OK)
		fprintf(stderr, "%s\n", lua_tostring(L, -1));
	CHECK(rc == LUA_OK);
	CHECK(lua_gettop(L) == 0);
}

static uint64_t _numeric_limit(void)
{
	lua_Number number = 1;

	/* Probe the configured float precision, capped by the documented range. */
	while ((number < UINT64_C(9007199254740992)) &&
	       ((lua_Number) (number + 1) != number))
		number *= 2;
	return (uint64_t) number - 1;
}

static void _check_value(uint64_t expected)
{
	if (expected == NO_VAL64 || expected == INFINITE64) {
		CHECK(lua_type(L, -1) == LUA_TNUMBER);
		CHECK(lua_tonumber(L, -1) ==
		      ((expected == NO_VAL64) ? -2 : -1));
	} else if (expected <= _numeric_limit()) {
		CHECK(lua_type(L, -1) == LUA_TNUMBER);
		CHECK(lua_tonumber(L, -1) == expected);
	} else {
		char decimal[32];

		snprintf(decimal, sizeof(decimal), "%" PRIu64, expected);
		CHECK(lua_type(L, -1) == LUA_TSTRING);
		CHECK(!strcmp(lua_tostring(L, -1), decimal));
	}
	lua_pop(L, 1);
}

static void _test_getters_and_roundtrip(void)
{
	const uint64_t values[] = {
		NO_VAL64,
		INFINITE64,
		0,
		1,
		1025,
		UINT64_C(16777215),
		UINT64_C(16777216),
		UINT64_C(16777217),
		INT32_MAX,
		UINT64_C(9007199254740991),
		UINT64_C(9007199254740992),
		UINT64_C(9007199254740993),
		INT64_MAX,
		MEM_PER_CPU,
		MEM_PER_CPU | 1,
		MEM_PER_CPU | 1025,
		UINT64_MAX - 2,
	};
	job_desc_msg_t desc;
	job_details_t details = { 0 };
	job_record_t record = { .details = &details };

	slurm_init_job_desc_msg(&desc);
	CHECK(desc.pn_min_memory == NO_VAL64);
	for (int modify = 0; modify < 2; modify++) {
		_push_job_desc(&desc, modify);
		lua_setglobal(L, "job_desc");
		for (size_t i = 0; i < sizeof(values) / sizeof(values[0]);
		     i++) {
			desc.pn_min_memory = values[i];
			details.pn_min_memory = values[i];
			CHECK(_get_job_req_field(&desc, "pn_min_memory") == 1);
			_check_value(values[i]);
			CHECK(slurm_lua_job_record_field(L, &record,
							 "pn_min_memory") == 1);
			_check_value(values[i]);
			_run("job_desc.pn_min_memory = job_desc.pn_min_memory");
			CHECK(desc.pn_min_memory == values[i]);
		}
	}
	record.details = NULL;
	CHECK(slurm_lua_job_record_field(L, &record, "pn_min_memory") == 1);
	_check_value(NO_VAL64);
}

static void _test_assignments(void)
{
	job_desc_msg_t desc;
	char script[128];
	const uint64_t values[] = {
		0,
		1,
		UINT64_C(16777215),
		UINT64_C(16777216),
		UINT64_C(16777217),
		UINT64_C(9007199254740991),
		UINT64_C(9007199254740992),
		UINT64_C(9007199254740993),
		MEM_PER_CPU,
		MEM_PER_CPU | 1,
		UINT64_MAX - 2,
		NO_VAL64,
		INFINITE64,
	};

	slurm_init_job_desc_msg(&desc);
	_push_job_desc(&desc, false);
	lua_setglobal(L, "job_desc");
	for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		snprintf(script, sizeof(script),
			 "job_desc.pn_min_memory = '%" PRIu64 "'", values[i]);
		_run(script);
		CHECK(desc.pn_min_memory == values[i]);
	}
	_run("job_desc.pn_min_memory = '0001025'");
	CHECK(desc.pn_min_memory == 1025);
	_run("job_desc.pn_min_memory = -2");
	CHECK(desc.pn_min_memory == NO_VAL64);
	_run("job_desc.pn_min_memory = -1");
	CHECK(desc.pn_min_memory == INFINITE64);
	snprintf(script, sizeof(script),
		 "job_desc.pn_min_memory = %" PRIu64 ".0", _numeric_limit());
	_run(script);
	CHECK(desc.pn_min_memory == _numeric_limit());
#if LUA_VERSION_NUM >= 503
	/* Native Lua integers must not be routed through lua_Number. */
	_run("job_desc.pn_min_memory = 16777217");
	CHECK(desc.pn_min_memory == UINT64_C(16777217));
	if (sizeof(lua_Integer) >= sizeof(int64_t)) {
		_run("job_desc.pn_min_memory = 9007199254740993");
		CHECK(desc.pn_min_memory == UINT64_C(9007199254740993));
		_run("job_desc.pn_min_memory = 9223372036854775807");
		CHECK(desc.pn_min_memory == INT64_MAX);
	}
#endif
}

static void _test_invalid_assignments(void)
{
	job_desc_msg_t desc;

	slurm_init_job_desc_msg(&desc);
	desc.pn_min_memory = 1025;
	_push_job_desc(&desc, false);
	lua_setglobal(L, "job_desc");
	lua_pushnumber(L, _numeric_limit() + 1);
	lua_setglobal(L, "unsafe_float");
	_run("local bad = {true,false,{},-3,-0.5,0.5,0/0,math.huge,-math.huge,"
	     " unsafe_float,9007199254740992.0,9223372036854775808.0,18446744073709551616.0,"
	     " '', '-2','-1','-0','+1',' 1','1 ','1.0','1e3','0xff',"
	     " '18446744073709551616','999999999999999999999999999999',"
	     " '1'..string.char(0),'1'..string.char(0)..'2'}\n"
	     "for _, value in ipairs(bad) do\n"
	     " local ok, err = pcall(function() job_desc.pn_min_memory = value end)\n"
	     " assert(not ok and type(err) == 'string')\n"
	     " assert(job_desc.pn_min_memory == 1025)\n"
	     "end\n"
	     "assert(not pcall(function() job_desc.pn_min_memory = nil end))\n");
	CHECK(desc.pn_min_memory == 1025);
}

static void _test_recommended_memory_fields(void)
{
	job_desc_msg_t desc;

	slurm_init_job_desc_msg(&desc);
	_push_job_desc(&desc, false);
	lua_setglobal(L, "job_desc");
	_run("assert(job_desc.min_mem_per_node == nil); "
	     "assert(job_desc.min_mem_per_cpu == nil); "
	     "job_desc.min_mem_per_node = 0; "
	     "assert(job_desc.min_mem_per_node == 0); "
	     "assert(job_desc.min_mem_per_cpu == nil)");
	CHECK(desc.pn_min_memory == 0);
	_run("job_desc.min_mem_per_cpu = 1025; "
	     "assert(job_desc.min_mem_per_cpu == 1025); "
	     "assert(job_desc.min_mem_per_node == nil); "
	     "assert(job_desc.pn_min_memory == '9223372036854776833')");
	CHECK(desc.pn_min_memory == (MEM_PER_CPU | 1025));
	_run("job_desc.min_mem_per_node = 1025; "
	     "assert(job_desc.min_mem_per_node == 1025); "
	     "assert(job_desc.min_mem_per_cpu == nil)");
	CHECK(desc.pn_min_memory == 1025);
}

int main(void)
{
	L = luaL_newstate();
	CHECK(L != NULL);
	luaL_openlibs(L);
	_test_getters_and_roundtrip();
	_test_assignments();
	_test_invalid_assignments();
	_test_recommended_memory_fields();
	lua_close(L);
	puts("Lua packed memory: all tests passed");
	return EXIT_SUCCESS;
}
