/*****************************************************************************\
 *  job_submit_lua_associations-test.c - Lua association snapshot tests.
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
#include <string.h>

#include "src/common/xmalloc.h"

/* Unlike assert(), these checks also run in builds configured with NDEBUG. */
#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, \
				#condition); \
			exit(EXIT_FAILURE); \
		} \
	} while (0)

static unsigned int lock_depth, lock_calls, unlock_calls;
static bool after_unlock;
static void (*after_unlock_action)(void);
static int native_fail_after = -1;
static void *native_allocations[256];
static size_t native_live;

/* Inject allocation failures into the real copying code and track its owners.
 * The list implementation and fixture setup still use Slurm's real allocator.
 */
static void *_test_xcalloc(size_t count, size_t size, bool clear, bool try,
			   const char *file, int line, const char *function)
{
	void *result;

	if (try) {
		CHECK(lock_depth == 1);
		if (native_fail_after == 0)
			return NULL;
		if (native_fail_after > 0)
			native_fail_after--;
	}
	result = slurm_xcalloc(count, size, clear, try, file, line, function);
	if (try && result) {
		CHECK(native_live < sizeof(native_allocations) /
					    sizeof(native_allocations[0]));
		native_allocations[native_live++] = result;
	}
	return result;
}

static void _test_xfree(void **pointer)
{
	for (size_t i = 0; *pointer && i < native_live; i++) {
		if (native_allocations[i] == *pointer) {
			native_allocations[i] =
				native_allocations[--native_live];
			break;
		}
	}
	slurm_xfree(pointer);
}

/* Exercise the actual static API, not a separate implementation of it. */
#define slurm_xcalloc _test_xcalloc
#define slurm_xfree _test_xfree
#include "src/plugins/job_submit/lua/job_submit_lua.c"
#undef slurm_xcalloc
#undef slurm_xfree

#ifndef LUA_OK
#define LUA_OK 0
#endif

list_t *assoc_mgr_assoc_list;

void assoc_mgr_lock(assoc_mgr_lock_t *locks)
{
	CHECK(lock_depth == 0);
	CHECK(locks->assoc == READ_LOCK);
	CHECK(!locks->file && !locks->qos && !locks->res && !locks->tres &&
	      !locks->user && !locks->wckey);
	lock_depth++;
	lock_calls++;
}

void assoc_mgr_unlock(assoc_mgr_lock_t *locks)
{
	CHECK(lock_depth == 1 && locks->assoc == READ_LOCK);
	lock_depth--;
	unlock_calls++;
	after_unlock = true;
	if (after_unlock_action) {
		void (*action)(void) = after_unlock_action;

		after_unlock_action = NULL;
		action();
	}
}

typedef struct {
	int fail_after;
	unsigned int failures, gc_calls;
} lua_allocator_t;

static void *_lua_allocator(void *arg, void *ptr, size_t old, size_t size)
{
	lua_allocator_t *allocator = arg;

	/* Every Lua allocator invocation must happen outside the association lock. */
	CHECK(lock_depth == 0);
	if (!size) {
		free(ptr);
		return NULL;
	}
	if (after_unlock && allocator->fail_after >= 0 &&
	    (!ptr || size > old)) {
		if (!allocator->fail_after) {
			allocator->failures++;
			return NULL;
		}
		allocator->fail_after--;
	}
	return realloc(ptr, size);
}

static int _count_snapshot_gc(lua_State *st)
{
	void *arg;
	assoc_snapshot_t *snapshot = lua_touserdata(st, 1);

	lua_getallocf(st, &arg);
	((lua_allocator_t *) arg)->gc_calls++;
	CHECK(_assoc_snapshot_gc(st) == 0);
	CHECK(snapshot->rows == NULL && snapshot->count == 0);
	return 0;
}

static lua_State *_new_state(lua_allocator_t *allocator)
{
	lua_State *st;

	*allocator = (lua_allocator_t) { .fail_after = -1 };
	after_unlock = false;
#if LUA_VERSION_NUM >= 505
	st = lua_newstate(_lua_allocator, allocator, 0);
#else
	st = lua_newstate(_lua_allocator, allocator);
#endif
	CHECK(st != NULL);
	luaL_openlibs(st);
	_register_assoc_snapshot(st);
	luaL_getmetatable(st, ASSOC_SNAPSHOT_METATABLE);
	lua_pushcfunction(st, _count_snapshot_gc);
	lua_setfield(st, -2, "__gc");
	lua_pop(st, 1);
	lua_newtable(st);
	lua_pushcfunction(st, _get_associations);
	lua_setfield(st, -2, "get_associations");
	lua_pushnumber(st, SLURMDB_FS_USE_PARENT);
	lua_setfield(st, -2, "FS_USE_PARENT");
	lua_pushnumber(st, NO_VAL);
	lua_setfield(st, -2, "NO_VAL");
	lua_pushnumber(st, INFINITE);
	lua_setfield(st, -2, "INFINITE");
	lua_setglobal(st, "slurm");
	return st;
}

static void _run(lua_State *st, const char *script)
{
	int rc = luaL_loadstring(st, script);

	if (rc == LUA_OK)
		rc = lua_pcall(st, 0, 0, 0);
	if (rc != LUA_OK)
		fprintf(stderr, "Lua test failed: %s\n", lua_tostring(st, -1));
	CHECK(rc == LUA_OK);
	CHECK(lock_depth == 0 && lock_calls == unlock_calls);
}

static void _delete_assoc(void *ptr)
{
	slurmdb_assoc_rec_t *assoc = ptr;

	free(assoc->acct);
	free(assoc->user);
	free(assoc->partition);
	free(assoc);
}

static slurmdb_assoc_rec_t *_add_assoc(uint32_t id, uint32_t parent_id,
				       const char *account, const char *user,
				       uint32_t uid, const char *partition,
				       uint32_t shares)
{
	slurmdb_assoc_rec_t *assoc = calloc(1, sizeof(*assoc));

	CHECK(assoc != NULL);
	assoc->id = id;
	assoc->parent_id = parent_id;
	assoc->acct = strdup(account);
	assoc->user = user ? strdup(user) : NULL;
	assoc->partition = partition ? strdup(partition) : NULL;
	assoc->uid = uid;
	assoc->shares_raw = shares;
	CHECK(assoc->acct && (!user || assoc->user) &&
	      (!partition || assoc->partition));
	list_append(assoc_mgr_assoc_list, assoc);
	return assoc;
}

static slurmdb_assoc_rec_t *_fixture(void)
{
	slurmdb_assoc_rec_t *alice;

	CHECK(!assoc_mgr_assoc_list && !native_live);
	assoc_mgr_assoc_list = list_create(_delete_assoc);
	_add_assoc(1, NO_VAL, "root", NULL, NO_VAL, NULL, NO_VAL);
	_add_assoc(2, 1, "alpha", NULL, NO_VAL, NULL, 1000);
	_add_assoc(3, 1, "beta", NULL, NO_VAL, NULL, 0);
	alice = _add_assoc(4, 2, "alpha", "alice", 1001, NULL, 7);
	_add_assoc(5, 3, "beta", "alice", 1001, "gpu", SLURMDB_FS_USE_PARENT);
	_add_assoc(6, 2, "alpha", "bob", 1002, "", 0);
	_add_assoc(7, 1, "root", "root", 0, NULL, INFINITE);
	_add_assoc(8, 3, "beta", "unresolved", NO_VAL, NULL, NO_VAL);
	return alice;
}

static void _clear_fixture(void)
{
	if (assoc_mgr_assoc_list)
		list_destroy(assoc_mgr_assoc_list);
	assoc_mgr_assoc_list = NULL;
}

static void _test_schema_and_filters(void)
{
	lua_allocator_t allocator;
	lua_State *st = _new_state(&allocator);

	_fixture();
	_run(st,
	     "function by_id(rows) local result = {}; for _, row in ipairs(rows) do "
	     " assert(not result[row.id]); result[row.id] = row end; return result end\n"
	     "local rows = slurm.get_associations(); assert(#rows == 8)\n"
	     "assert(#slurm.get_associations(nil) == 8)\n"
	     "assert(#slurm.get_associations({}) == 8)\n"
	     "local r = by_id(rows)\n"
	     "assert(r[1].account == 'root' and r[1].parent_id == slurm.NO_VAL)\n"
	     "assert(r[1].user == nil and r[1].uid == nil and r[1].partition == nil)\n"
	     "assert(r[2].shares_raw == 1000 and r[3].shares_raw == 0)\n"
	     "assert(r[4].user == 'alice' and r[4].uid == 1001 and r[4].parent_id == 2)\n"
	     "assert(r[5].account == 'beta' and r[5].partition == 'gpu')\n"
	     "assert(r[5].shares_raw == slurm.FS_USE_PARENT)\n"
	     "assert(r[6].partition == nil and r[6].shares_raw == 0)\n"
	     "assert(r[7].user == 'root' and r[7].uid == 0)\n"
	     "assert(r[7].shares_raw == slurm.INFINITE)\n"
	     "assert(r[8].uid == slurm.NO_VAL and r[8].shares_raw == slurm.NO_VAL)\n"
	     "local keys = {id=true,parent_id=true,account=true,user=true,uid=true,"
	     " partition=true,shares_raw=true}\n"
	     "for _, row in ipairs(rows) do for key in pairs(row) do assert(keys[key]) end end\n"
	     "rows = slurm.get_associations({uid=1001}); assert(#rows == 5)\n"
	     "r = by_id(rows); assert(r[1] and r[2] and r[3] and r[4] and r[5])\n"
	     "assert(not r[6] and not r[7] and not r[8])\n"
	     "rows = slurm.get_associations({uid=1001,include_accounts=false})\n"
	     "r = by_id(rows); assert(#rows == 2 and r[4] and r[5])\n"
	     "assert(#slurm.get_associations({include_accounts=false}) == 5)\n"
	     "rows = slurm.get_associations({uid=0}); r = by_id(rows)\n"
	     "assert(#rows == 4 and r[7] and r[1] and not r[4])\n"
	     "assert(#slurm.get_associations({uid=12345}) == 3)\n"
	     "assert(#slurm.get_associations({uid=12345,include_accounts=false}) == 0)\n"
	     "assert(#slurm.get_associations({uid=slurm.NO_VAL-1}) == 3)\n");
	CHECK(native_live == 0);
	lua_close(st);
	_clear_fixture();
}

static void _test_invalid_options(void)
{
	lua_allocator_t allocator;
	lua_State *st = _new_state(&allocator);
	unsigned int previous = lock_calls;

	_run(st,
	     "local bad = {true,false,1,'1001',"
	     " {uid='1001'},{uid=true},{uid={}},{uid=-1},{uid=1.5},"
	     " {uid=0/0},{uid=math.huge},{uid=-math.huge},"
	     " {uid=slurm.NO_VAL},{uid=slurm.NO_VAL+1},"
	     " {include_accounts=1},{include_accounts='false'},"
	     " {unknown=true},{[1]=1001},{[false]=true},{['uid'..string.char(0)]=1}}\n"
	     "for _, value in ipairs(bad) do\n"
	     " local ok, message = pcall(slurm.get_associations, value)\n"
	     " assert(not ok and type(message) == 'string')\n"
	     "end\n"
	     "assert(not pcall(slurm.get_associations, nil, {}))\n");
	CHECK(lock_calls == previous && native_live == 0);
	lua_close(st);
}

static void _test_missing_and_empty(void)
{
	lua_allocator_t allocator;
	lua_State *st = _new_state(&allocator);

	CHECK(assoc_mgr_assoc_list == NULL);
	_run(st, "local rows, err = slurm.get_associations(); "
		 "assert(rows == nil and type(err) == 'string' and #err > 0)");
	assoc_mgr_assoc_list = list_create(_delete_assoc);
	_run(st, "local rows, err = slurm.get_associations(); "
		 "assert(type(rows) == 'table' and #rows == 0 and err == nil)");
	CHECK(native_live == 0);
	lua_close(st);
	_clear_fixture();
}

static void _test_copy_lifetime(void)
{
	lua_allocator_t allocator;
	lua_State *st = _new_state(&allocator);
	slurmdb_assoc_rec_t *alice = _fixture();

	_run(st,
	     "saved = slurm.get_associations({uid=1001,include_accounts=false})");
	free(alice->acct);
	alice->acct = strdup("renamed");
	CHECK(alice->acct != NULL);
	alice->shares_raw = 99;
	_run(st,
	     "assert(saved[1].account == 'alpha' and saved[1].shares_raw == 7); "
	     "local fresh = slurm.get_associations({uid=1001,include_accounts=false}); "
	     "assert(fresh[1].account == 'renamed' and fresh[1].shares_raw == 99); "
	     "fresh[1].shares_raw = 123");
	CHECK(alice->shares_raw == 99);
	_clear_fixture();
	_run(st,
	     "assert(saved[1].account == 'alpha' and saved[2].user == 'alice'); "
	     "assert(saved[2].partition == 'gpu' and saved[2].shares_raw == slurm.FS_USE_PARENT)");
	CHECK(native_live == 0);
	lua_close(st);
}

static void _test_native_allocation_failure(void)
{
	unsigned int failures = 0;

	_fixture();
	for (int fail = 0; fail < 64; fail++) {
		lua_allocator_t allocator;
		lua_State *st = _new_state(&allocator);
		int rc;
		bool succeeded;

		lua_pushcfunction(st, _get_associations);
		native_fail_after = fail;
		rc = lua_pcall(st, 0, LUA_MULTRET, 0);
		native_fail_after = -1;
		CHECK(rc == LUA_OK && lock_depth == 0 &&
		      lock_calls == unlock_calls);
		succeeded = lua_istable(st, 1);
		if (!succeeded) {
			CHECK(lua_gettop(st) == 2 && lua_isnil(st, 1) &&
			      lua_type(st, 2) == LUA_TSTRING);
			failures++;
		}
		CHECK(native_live == 0);
		lua_close(st);
		if (succeeded)
			break;
		CHECK(fail != 63);
	}
	CHECK(failures > 1);
	_clear_fixture();
}

static void _test_cache_deleted_on_unlock(void)
{
	lua_allocator_t allocator;
	lua_State *st = _new_state(&allocator);

	_fixture();
	/* Simulate a cache update as soon as its read lock has been released. */
	after_unlock_action = _clear_fixture;
	_run(st,
	     "local rows = slurm.get_associations(); assert(#rows == 8); "
	     "assert(rows[4].account == 'alpha' and rows[4].user == 'alice'); "
	     "assert(rows[5].partition == 'gpu' and "
	     "rows[5].shares_raw == slurm.FS_USE_PARENT)");
	CHECK(assoc_mgr_assoc_list == NULL && after_unlock_action == NULL);
	CHECK(native_live == 0);
	lua_close(st);
}

static void _test_lua_allocation_failure(void)
{
	unsigned int failures = 0;

	_fixture();
	for (int fail = 0; fail < 256; fail++) {
		lua_allocator_t allocator;
		lua_State *st = _new_state(&allocator);
		int rc;

		after_unlock = false;
		lua_pushcfunction(st, _get_associations);
		allocator.fail_after = fail;
		rc = lua_pcall(st, 0, 1, 0);
		allocator.fail_after = -1;
		CHECK(lock_depth == 0 && lock_calls == unlock_calls &&
		      after_unlock);
		if (rc != LUA_OK) {
			CHECK(rc == LUA_ERRMEM && allocator.failures > 0);
			failures++;
		} else {
			CHECK(lua_istable(st, -1));
		}
		lua_settop(st, 0);
		lua_gc(st, LUA_GCCOLLECT, 0);
		CHECK(allocator.gc_calls == 1 && native_live == 0);
		/* A failed conversion must not leave the API or its locks unusable. */
		_run(st, "assert(#slurm.get_associations() == 8)");
		lua_close(st);
		CHECK(native_live == 0);
		if (rc == LUA_OK)
			break;
		CHECK(fail != 255);
	}
	CHECK(failures > 1);
	_clear_fixture();
}

int main(void)
{
	_test_schema_and_filters();
	_test_invalid_options();
	_test_missing_and_empty();
	_test_copy_lifetime();
	_test_cache_deleted_on_unlock();
	_test_native_allocation_failure();
	_test_lua_allocation_failure();
	CHECK(lock_calls > 0 && lock_calls == unlock_calls && !native_live);
	puts("Lua association snapshots: all tests passed");
	return EXIT_SUCCESS;
}
