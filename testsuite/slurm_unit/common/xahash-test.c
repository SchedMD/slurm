/*****************************************************************************\
 *  Copyright (C) 2024 SchedMD LLC
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

#include <stdint.h>
#include <check.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xahash.h"

#define FIXED_STATE_ENTRIES 1024
#define FIXED_STATE_OVERCOMMIT_ENTRIES 512
#define KEY_SIZE sizeof(void *)

#define GLOBAL_STATE_MAGIC 0xeae0eef0
typedef struct {
	int magic; /* GLOBAL_STATE_MAGIC */
} global_state_t;

#define STATE_MAGIC 0xaa10e8f0
#define STATE_CALLER_MAGIC 0xba0088f0
typedef struct {
	int magic; /* STATE_MAGIC */
	void *key;
	int caller_magic;
} state_t;

static xahash_hash_t _hash(const void *key, const size_t key_bytes, void *state)
{
	global_state_t *gs = state;

	ck_assert(gs->magic == GLOBAL_STATE_MAGIC);
	ck_assert(key_bytes == KEY_SIZE);

	/* just use the pointer to make a hash */
	return (((uintptr_t) key) >> 32) ^ ((uintptr_t) key);
}

static bool _match(void *ptr, const void *key, const size_t key_bytes, void *state)
{
	global_state_t *gs = state;
	state_t *s = ptr;

	ck_assert(gs->magic == GLOBAL_STATE_MAGIC);
	ck_assert(s->magic == STATE_MAGIC);
	ck_assert(key_bytes == KEY_SIZE);

	if (s->key == key)
		return true;
	else
		return false;
}

static void _on_insert(void *ptr, const void *key, const size_t key_bytes, void *state)
{
	global_state_t *gs = state;
	state_t *s = ptr;

	ck_assert(gs->magic == GLOBAL_STATE_MAGIC);
	ck_assert(s != NULL);
	ck_assert(key_bytes == KEY_SIZE);

	*s = (state_t) {
		.magic = STATE_MAGIC,
		.key = (void *) key,
	};
}

static void _on_free(void *ptr, void *state)
{
	global_state_t *gs = state;
	state_t *s = ptr;

	ck_assert(gs->magic == GLOBAL_STATE_MAGIC);
	ck_assert(s != NULL);
	ck_assert(s->magic == STATE_MAGIC);

	*s = (state_t) {
		.magic = ~STATE_MAGIC,
		.key = NULL,
	};
}

static xahash_foreach_control_t _foreach(void *entry, void *state, void *arg)
{
	global_state_t *gs = state;
	state_t *s = entry;

	ck_assert(gs != NULL);
	ck_assert(gs->magic == GLOBAL_STATE_MAGIC);
	ck_assert(s != NULL);
	ck_assert(s->magic == STATE_MAGIC);

	return XAHASH_FOREACH_CONT;
}

START_TEST(test_fixed_basic)
{
	xahash_table_t *ht;
	global_state_t *gs;
	state_t *s, *f;

	ht = xahash_new_table(_hash, _match, _on_insert, _on_free,
			      sizeof(global_state_t), sizeof(state_t),
			      FIXED_STATE_ENTRIES);

	ck_assert_msg(ht != NULL, "hashtable created");

	/* check global state table works */
	gs = xahash_get_state_ptr(ht);

	ck_assert_msg(ht != NULL, "hashtable state created");

	*gs = (global_state_t) {
		.magic = GLOBAL_STATE_MAGIC,
	};
	ck_assert(gs->magic == GLOBAL_STATE_MAGIC);

	/* verify we get the same pointer back */
	gs = xahash_get_state_ptr(ht);
	ck_assert(gs->magic == GLOBAL_STATE_MAGIC);

	/* verify we don't find anything in an empty table */
	ck_assert(!xahash_find_entry(ht, NULL, KEY_SIZE));
	ck_assert(!xahash_find_entry(ht, _match, KEY_SIZE));
	ck_assert(!xahash_free_entry(ht, _match, KEY_SIZE));
	ck_assert(!xahash_find_entry(ht, &ht, KEY_SIZE));
	ck_assert(!xahash_free_entry(ht, &ht, KEY_SIZE));

	/* try inserts */
	s = xahash_insert_entry(ht, &s, KEY_SIZE);
	ck_assert(s->magic == STATE_MAGIC);
	ck_assert(s->key == &s);
	ck_assert(s->caller_magic == 0);
	s->caller_magic = STATE_CALLER_MAGIC;

	/* verify we don't find invalid keys with entries */
	ck_assert(!xahash_find_entry(ht, NULL, KEY_SIZE));
	ck_assert(!xahash_find_entry(ht, _match, KEY_SIZE));
	ck_assert(!xahash_find_entry(ht, &ht, KEY_SIZE));

	/* verify we can find new entry */
	f = xahash_find_entry(ht, &s, KEY_SIZE);
	ck_assert(f != NULL);
	ck_assert(f == s);
	ck_assert(f->magic == STATE_MAGIC);
	ck_assert(f->key == &s);
	ck_assert(f->caller_magic == STATE_CALLER_MAGIC);
	ck_assert(s->magic == STATE_MAGIC);
	ck_assert(s->key == &s);
	ck_assert(s->caller_magic == STATE_CALLER_MAGIC);

	ck_assert(xahash_free_entry(ht, &s, KEY_SIZE));
	ck_assert(!xahash_free_entry(ht, &s, KEY_SIZE));
	ck_assert(!xahash_find_entry(ht, &s, KEY_SIZE));

	/* verify we get the same pointer and value back */
	gs = xahash_get_state_ptr(ht);
	ck_assert(gs->magic == GLOBAL_STATE_MAGIC);

	FREE_NULL_XAHASH_TABLE(ht);
}
END_TEST

START_TEST(test_fixed_mass)
{
	global_state_t *gs;
	xahash_table_t *ht;
	state_t *s[FIXED_STATE_ENTRIES + FIXED_STATE_OVERCOMMIT_ENTRIES] = {0};

	ht = xahash_new_table(_hash, _match, _on_insert, _on_free,
			      sizeof(global_state_t), sizeof(state_t),
			      FIXED_STATE_ENTRIES);
	gs = xahash_get_state_ptr(ht);
	*gs = (global_state_t) {
		.magic = GLOBAL_STATE_MAGIC,
	};

	/* insert all entries */
	for (int i = 0; i < ARRAY_SIZE(s); i++) {
		state_t *f;
		int caller_magic = STATE_CALLER_MAGIC * ((uint64_t) &s[i]);

		/* try inserts */
		f = s[i] = xahash_insert_entry(ht, &s[i], KEY_SIZE);

		/* verify on_insert changes */
		ck_assert(f->magic == STATE_MAGIC);
		ck_assert(f->key == &s[i]);
		ck_assert(f->caller_magic == 0);
		f->caller_magic = caller_magic;

		/* verify we can still find via same key */
		ck_assert(f == xahash_find_entry(ht, (s + i), KEY_SIZE));
		ck_assert(f == xahash_find_entry(ht, &s[i], KEY_SIZE));
		ck_assert(s[i]->caller_magic == caller_magic);
		ck_assert(f == s[i]);
	}

	/* verify all entries and blobs */
	for (int i = 0; i < ARRAY_SIZE(s); i++) {
		int caller_magic = STATE_CALLER_MAGIC * ((uint64_t) &s[i]);

		/* verify we can find every entry */
		state_t *f = xahash_find_entry(ht, &s[i], KEY_SIZE);
		ck_assert(f != NULL);
		ck_assert(f == s[i]);
		ck_assert(f->magic == STATE_MAGIC);
		ck_assert(f->key == &s[i]);
		ck_assert(f->caller_magic == s[i]->caller_magic);
		ck_assert(f->caller_magic == caller_magic);
	}

	/* verify all entries and blobs with foreach */
	ck_assert(xahash_foreach_entry(ht, _foreach, NULL) == ARRAY_SIZE(s));

	/* verify all entries and blobs (in reverse) */
	for (int i = (ARRAY_SIZE(s) - 1); i >= 0; i--) {
		int caller_magic = STATE_CALLER_MAGIC * ((uint64_t) &s[i]);

		/* verify we can find every entry */
		state_t *f = xahash_find_entry(ht, &s[i], KEY_SIZE);
		ck_assert(f != NULL);
		ck_assert(f == s[i]);
		ck_assert(f->magic == STATE_MAGIC);
		ck_assert(f->key == &s[i]);
		ck_assert(f->caller_magic == s[i]->caller_magic);
		ck_assert(f->caller_magic == caller_magic);
	}

	/* remove and verify all entries removed */
	for (int i = 0; i < ARRAY_SIZE(s); i++) {
		ck_assert(xahash_find_entry(ht, &s[i], KEY_SIZE) == s[i]);
		s[i] = NULL;
		ck_assert(xahash_free_entry(ht, &s[i], KEY_SIZE));
		ck_assert(!xahash_find_entry(ht, &s[i], KEY_SIZE));
	}

	/* verify all removed (again) */
	for (int i = 0; i < ARRAY_SIZE(s); i++)
		ck_assert(!xahash_find_entry(ht, &s[i], KEY_SIZE));

	/* verify we get the same pointer and value back */
	ck_assert(gs == xahash_get_state_ptr(ht));
	gs = xahash_get_state_ptr(ht);
	ck_assert(gs->magic == GLOBAL_STATE_MAGIC);

	FREE_NULL_XAHASH_TABLE(ht);
}
END_TEST

Suite *suite_xahash(void)
{
	Suite *s = suite_create("xahash");
	TCase *tc_core = tcase_create("xahash");

	tcase_add_test(tc_core, test_fixed_basic);
	tcase_add_test(tc_core, test_fixed_mass);

	suite_add_tcase(s, tc_core);
	return s;
}

int main(void)
{
	int number_failed;
	enum print_output po = CK_ENV;
	enum fork_status fs = CK_FORK_GETENV;
	SRunner *sr = NULL;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");

	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);

	log_init("xahash-test", log_opts, 0, NULL);

	if (log_opts.stderr_level >= LOG_LEVEL_DEBUG) {
		/* automatically be gdb friendly when debug logging */
		po = CK_VERBOSE;
		fs = CK_NOFORK;
	}

	sr = srunner_create(suite_xahash());
	srunner_set_fork_status(sr, fs);
	srunner_run_all(sr, po);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
