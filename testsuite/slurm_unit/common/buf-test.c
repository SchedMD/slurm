/*****************************************************************************\
 *  buf-test.c - unit tests for the buf_t lifecycle and accessors
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

#include <check.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"

/*
 * Tests for the buf_t life cycle and accessors in src/common/pack.c.
 * The pack*()/unpack*() serializers are covered by pack-test.c.
 */

/* Verify buffer is empty and sized as requested */
static void _check_empty_buf(buf_t *buf, const uint32_t size)
{
	ck_assert(buf != NULL);
	ck_assert(get_buf_data(buf) != NULL);
	ck_assert_int_eq(size_buf(buf), size);
	ck_assert_int_eq(get_buf_offset(buf), 0);
	ck_assert_int_eq(remaining_buf(buf), size);
}

START_TEST(test_init_buf)
{
	buf_t *buf = init_buf(128);
	const char zeros[128] = { 0 };

	_check_empty_buf(buf, 128);
	/* init_buf() uses xmalloc() which always zeros the allocation */
	ck_assert_msg(!memcmp(get_buf_data(buf), zeros, sizeof(zeros)),
		      "init_buf() did not zero the allocation");
	free_buf(buf);

	/* a size of zero must fall back to the BUF_SIZE default */
	buf = init_buf(0);
	_check_empty_buf(buf, BUF_SIZE);
	free_buf(buf);

	/* free_buf() must accept NULL */
	free_buf(NULL);

	buf = init_buf(0);
	FREE_NULL_BUFFER(buf);
	ck_assert(buf == NULL);
	/* FREE_NULL_BUFFER() must be a no-op when already NULL */
	FREE_NULL_BUFFER(buf);
	ck_assert(buf == NULL);
}

END_TEST

START_TEST(test_try_init_buf)
{
	buf_t *buf = try_init_buf(128);

	_check_empty_buf(buf, 128);
	free_buf(buf);

	/* a size of zero must fall back to the BUF_SIZE default */
	buf = try_init_buf(0);
	_check_empty_buf(buf, BUF_SIZE);
	free_buf(buf);

	/* oversized request must be rejected instead of fatal()ing */
	ck_assert(try_init_buf(MAX_BUF_SIZE + 1) == NULL);
}

END_TEST

START_TEST(test_create_buf)
{
	char *data = xmalloc(16);
	buf_t *buf;

	memcpy(data, "0123456789abcdef", 16);

	buf = create_buf(data, 16);
	ck_assert(buf != NULL);
	/* create_buf() takes ownership of data instead of copying it */
	ck_assert(get_buf_data(buf) == data);
	ck_assert_int_eq(size_buf(buf), 16);
	ck_assert_int_eq(get_buf_offset(buf), 0);
	ck_assert_int_eq(remaining_buf(buf), 16);
	free_buf(buf);

	/* oversized request must be rejected */
	ck_assert(create_buf(NULL, MAX_BUF_SIZE + 1) == NULL);
}

END_TEST

START_TEST(test_create_shadow_buf)
{
	char data[] = "shadowed";
	buf_t *buf = create_shadow_buf(data, (sizeof(data) - 1));

	ck_assert(buf != NULL);
	ck_assert(get_buf_data(buf) == data);
	ck_assert_int_eq(size_buf(buf), (sizeof(data) - 1));
	ck_assert_int_eq(get_buf_offset(buf), 0);

	/* shadow buffers do not own their memory and can not be grown */
	ck_assert_int_eq(try_grow_buf(buf, 1), EINVAL);
	ck_assert_int_eq(size_buf(buf), (sizeof(data) - 1));

	/*
	 * try_grow_buf_remaining() only rejects an append when it has to grow,
	 * so with room already remaining an append must still be rejected by
	 * buf_append_bytes()/buf_append_str()'s own shadow check, or it would
	 * write through to the caller's memory
	 */
	ck_assert_int_eq(buf_append_bytes(buf, "XXXX", 4), EINVAL);
	ck_assert_int_eq(buf_append_str(buf, "XXXX"), EINVAL);
	ck_assert_int_eq(get_buf_offset(buf), 0);
	ck_assert_str_eq(data, "shadowed");

	/* free_buf() must not release the shadowed memory */
	free_buf(buf);
	ck_assert_str_eq(data, "shadowed");

	ck_assert(create_shadow_buf(data, (MAX_BUF_SIZE + 1)) == NULL);
}

END_TEST

START_TEST(test_create_mmap_buf)
{
	const char contents[] = "mmap()ed buffer contents";
	const size_t bytes = (sizeof(contents) - 1);
	char path[PATH_MAX];
	int fd;
	buf_t *buf;

	snprintf(path, sizeof(path), "%s/buf-test-XXXXXX",
		 (getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp"));
	fd = mkstemp(path);

	ck_assert_int_ge(fd, 0);
	ck_assert_int_eq(write(fd, contents, bytes), bytes);
	ck_assert_int_eq(close(fd), 0);

	buf = create_mmap_buf(path);
	/* the mapping outlives the file, so unlink before asserting anything */
	ck_assert_int_eq(unlink(path), 0);

	ck_assert(buf != NULL);
	ck_assert_int_eq(size_buf(buf), bytes);
	ck_assert_int_eq(get_buf_offset(buf), 0);
	ck_assert_int_eq(remaining_buf(buf), bytes);
	ck_assert_msg(!memcmp(get_buf_data(buf), contents, bytes),
		      "mmap()ed buffer does not match the file contents");

	/* mmap()ed buffers are read only and can not be grown */
	ck_assert_int_eq(try_grow_buf(buf, 1), EINVAL);

	/*
	 * room remains in the mapping, so only buf_append_bytes()/
	 * buf_append_str()'s own mmaped check stops a write through a
	 * PROT_READ mapping -- try_grow_buf_remaining() alone would not
	 */
	ck_assert_int_eq(buf_append_bytes(buf, "X", 1), EINVAL);
	ck_assert_int_eq(buf_append_str(buf, "X"), EINVAL);
	ck_assert_int_eq(get_buf_offset(buf), 0);
	ck_assert_msg(!memcmp(get_buf_data(buf), contents, bytes),
		      "a rejected append must not modify the mapped contents");

	free_buf(buf);

	/* a file that can not be opened must not be fatal */
	ck_assert(create_mmap_buf(path) == NULL);
}

END_TEST

START_TEST(test_shadow_buf_initializer)
{
	char data[] = "shadowed";
	buf_t buf = SHADOW_BUF_INITIALIZER(data, (sizeof(data) - 1));

	ck_assert(get_buf_data(&buf) == data);
	ck_assert_int_eq(size_buf(&buf), (sizeof(data) - 1));
	/*
	 * Unlike create_shadow_buf(), the initializer marks the buffer as fully
	 * populated so it is ready to be read back
	 */
	ck_assert_int_eq(get_buf_offset(&buf), (sizeof(data) - 1));
	ck_assert_int_eq(remaining_buf(&buf), 0);

	/* the shadowed memory must never be grown or written past */
	ck_assert_int_eq(try_grow_buf(&buf, 1), EINVAL);
	ck_assert_int_eq(try_grow_buf_remaining(&buf, 1), EINVAL);
	ck_assert_int_eq(buf_append_bytes(&buf, "x", 1), EINVAL);
	ck_assert_int_eq(buf_append_str(&buf, "x"), EINVAL);
	ck_assert_int_eq(get_buf_offset(&buf), (sizeof(data) - 1));
	ck_assert_str_eq(data, "shadowed");
}

END_TEST

START_TEST(test_buf_macros)
{
	/*
	 * Every macro argument below is an expression that only parses (or only
	 * evaluates correctly) when the macro parenthesizes its parameters.
	 * These are regression guards for the get_buf_data(), get_buf_offset(),
	 * set_buf_offset(), remaining_buf() and size_buf() macros.
	 */
	char data[] = "shadowed";
	buf_t stack_buf = SHADOW_BUF_INITIALIZER(data, (sizeof(data) - 1));
	buf_t *buf = init_buf(64);
	buf_t **buf_ptr = &buf;
	void *ptr = buf;

	ck_assert(get_buf_data((buf_t *) ptr) == get_buf_data(buf));
	ck_assert_int_eq(get_buf_offset(*buf_ptr), 0);

	set_buf_offset(*buf_ptr, (get_buf_offset(buf) + 8));
	ck_assert_int_eq(get_buf_offset(buf), 8);

	ck_assert_int_eq(remaining_buf(&stack_buf), 0);
	ck_assert_int_eq(remaining_buf(buf ? buf : &stack_buf), (64 - 8));
	ck_assert_int_eq(size_buf(buf ? buf : &stack_buf), 64);
	ck_assert_int_eq(size_buf(&stack_buf), (sizeof(data) - 1));

	free_buf(buf);
}

END_TEST

START_TEST(test_grow_buf)
{
	buf_t *buf = init_buf(16);

	ck_assert_int_eq(buf_append_str(buf, "0123456789abcdef"),
			 SLURM_SUCCESS);
	ck_assert_int_eq(remaining_buf(buf), 0);

	/* grow_buf() grows by exactly the requested number of bytes */
	grow_buf(buf, 16);
	ck_assert_int_eq(size_buf(buf), 32);
	ck_assert_int_eq(get_buf_offset(buf), 16);
	ck_assert_int_eq(remaining_buf(buf), 16);
	/* existing contents must survive the resize */
	ck_assert_msg(!memcmp(get_buf_data(buf), "0123456789abcdef", 16),
		      "grow_buf() did not preserve the contents");

	free_buf(buf);
}

END_TEST

START_TEST(test_try_grow_buf)
{
	buf_t *buf = init_buf(16);
	/* buffer with no memory to avoid allocating MAX_BUF_SIZE bytes */
	buf_t full_buf = {
		.magic = BUF_MAGIC,
		.size = MAX_BUF_SIZE,
	};

	/*
	 * Growth is always at least BUF_SIZE to avoid a xrealloc() per append,
	 * and requests of BUF_SIZE or larger are added on top of that
	 */
	ck_assert_int_eq(try_grow_buf(buf, 1), SLURM_SUCCESS);
	ck_assert_int_eq(size_buf(buf), (16 + BUF_SIZE));
	ck_assert_int_eq(get_buf_offset(buf), 0);

	ck_assert_int_eq(try_grow_buf(buf, (BUF_SIZE - 1)), SLURM_SUCCESS);
	ck_assert_int_eq(size_buf(buf), (16 + (2 * BUF_SIZE)));

	ck_assert_int_eq(try_grow_buf(buf, BUF_SIZE), SLURM_SUCCESS);
	ck_assert_int_eq(size_buf(buf), (16 + (4 * BUF_SIZE)));

	free_buf(buf);

	/* growing past MAX_BUF_SIZE must be rejected without changing size */
	ck_assert_int_eq(try_grow_buf(&full_buf, 1), ESLURM_DATA_TOO_LARGE);
	ck_assert_int_eq(size_buf(&full_buf), MAX_BUF_SIZE);
}

END_TEST

START_TEST(test_try_grow_buf_remaining)
{
	buf_t *buf = init_buf(BUF_SIZE);
	/* buffer with no memory to avoid allocating MAX_BUF_SIZE bytes */
	buf_t full_buf = {
		.magic = BUF_MAGIC,
		.size = MAX_BUF_SIZE,
		.processed = MAX_BUF_SIZE,
	};

	/* an exact fit must not grow the buffer */
	ck_assert_int_eq(try_grow_buf_remaining(buf, BUF_SIZE), SLURM_SUCCESS);
	ck_assert_int_eq(size_buf(buf), BUF_SIZE);

	set_buf_offset(buf, 1);
	ck_assert_int_eq(remaining_buf(buf), (BUF_SIZE - 1));

	/* one byte short must grow the buffer */
	ck_assert_int_eq(try_grow_buf_remaining(buf, BUF_SIZE), SLURM_SUCCESS);
	ck_assert_int_eq(size_buf(buf), (3 * BUF_SIZE));
	/* growing must never move the read/write offset */
	ck_assert_int_eq(get_buf_offset(buf), 1);

	free_buf(buf);

	ck_assert_int_eq(try_grow_buf_remaining(&full_buf, 1),
			 ESLURM_DATA_TOO_LARGE);
	ck_assert_int_eq(size_buf(&full_buf), MAX_BUF_SIZE);
}

END_TEST

START_TEST(test_buf_append_bytes)
{
	const char bytes[] = { 'a', '\0', 'b', '\0', 'c' };
	buf_t *buf = init_buf(16);

	/* appending nothing is always a no-op success */
	ck_assert_int_eq(buf_append_bytes(buf, NULL, 0), SLURM_SUCCESS);
	ck_assert_int_eq(get_buf_offset(buf), 0);
	ck_assert_int_eq(size_buf(buf), 16);

	/* appending is binary safe */
	ck_assert_int_eq(buf_append_bytes(buf, bytes, sizeof(bytes)),
			 SLURM_SUCCESS);
	ck_assert_int_eq(get_buf_offset(buf), sizeof(bytes));
	ck_assert_int_eq(size_buf(buf), 16);
	ck_assert_msg(!memcmp(get_buf_data(buf), bytes, sizeof(bytes)),
		      "appended bytes do not match the source");

	/* appends are contiguous */
	ck_assert_int_eq(buf_append_bytes(buf, bytes, sizeof(bytes)),
			 SLURM_SUCCESS);
	ck_assert_int_eq(get_buf_offset(buf), (2 * sizeof(bytes)));
	ck_assert_msg(!memcmp((get_buf_data(buf) + sizeof(bytes)), bytes,
			      sizeof(bytes)),
		      "second append is not contiguous with the first");

	free_buf(buf);
}

END_TEST

START_TEST(test_buf_append_bytes_grow)
{
	char *chunk = xmalloc(BUF_SIZE);
	buf_t *buf = init_buf(BUF_SIZE);

	memset(chunk, 'a', BUF_SIZE);

	/* an exact fit must not grow the buffer */
	ck_assert_int_eq(buf_append_bytes(buf, chunk, BUF_SIZE), SLURM_SUCCESS);
	ck_assert_int_eq(size_buf(buf), BUF_SIZE);
	ck_assert_int_eq(get_buf_offset(buf), BUF_SIZE);
	ck_assert_int_eq(remaining_buf(buf), 0);

	/* a full buffer must grow to take another byte */
	ck_assert_int_eq(buf_append_bytes(buf, "b", 1), SLURM_SUCCESS);
	ck_assert_int_eq(size_buf(buf), (2 * BUF_SIZE));
	ck_assert_int_eq(get_buf_offset(buf), (BUF_SIZE + 1));
	/* contents must survive the implicit grow */
	ck_assert_msg(!memcmp(get_buf_data(buf), chunk, BUF_SIZE),
		      "buf_append_bytes() did not preserve the contents");
	ck_assert_int_eq(get_buf_data(buf)[BUF_SIZE], 'b');

	free_buf(buf);
	xfree(chunk);
}

END_TEST

START_TEST(test_buf_append_bytes_too_large)
{
	/* buffer with no memory to avoid allocating MAX_BUF_SIZE bytes */
	buf_t full_buf = {
		.magic = BUF_MAGIC,
		.size = MAX_BUF_SIZE,
		.processed = MAX_BUF_SIZE,
	};

	ck_assert_int_eq(buf_append_bytes(&full_buf, "x", 1),
			 ESLURM_DATA_TOO_LARGE);
	ck_assert_int_eq(buf_append_str(&full_buf, "x"), ESLURM_DATA_TOO_LARGE);
	/* a failed append must not advance the offset */
	ck_assert_int_eq(get_buf_offset(&full_buf), MAX_BUF_SIZE);

	/*
	 * A single request larger than MAX_BUF_SIZE must be rejected outright,
	 * even against a small buffer with room to spare
	 */
	{
		buf_t *buf = init_buf(16);

		ck_assert_int_eq(buf_append_bytes(buf, NULL,
						  ((size_t) MAX_BUF_SIZE) + 1),
				 ESLURM_DATA_TOO_LARGE);
		ck_assert_int_eq(get_buf_offset(buf), 0);
		ck_assert_int_eq(size_buf(buf), 16);

		free_buf(buf);
	}

	/*
	 * try_grow_buf_remaining() takes a uint32_t, so a bytes count that is
	 * itself a multiple of 2^32 truncates to 0 on the implicit conversion
	 * if the "bytes > MAX_BUF_SIZE" check above is ever lost -- remaining
	 * space then looks sufficient and the memcpy() below still copies the
	 * full untruncated count. Only reachable where size_t is wider than
	 * uint32_t; NULL is safe here because the guard must reject this
	 * before ever touching ptr.
	 */
	if (sizeof(size_t) > sizeof(uint32_t)) {
		buf_t *buf = init_buf(16);

		ck_assert_int_eq(buf_append_bytes(buf, NULL,
						  (((size_t) UINT32_MAX) + 1)),
				 ESLURM_DATA_TOO_LARGE);
		ck_assert_int_eq(get_buf_offset(buf), 0);
		ck_assert_int_eq(size_buf(buf), 16);

		free_buf(buf);
	}
}

END_TEST

START_TEST(test_buf_append_str)
{
	buf_t *buf = init_buf(16);

	/* a NULL string is treated like an empty one, not a fatal error */
	ck_assert_int_eq(buf_append_str(buf, NULL), SLURM_SUCCESS);
	ck_assert_int_eq(get_buf_offset(buf), 0);

	/* appending an empty string is a no-op success */
	ck_assert_int_eq(buf_append_str(buf, ""), SLURM_SUCCESS);
	ck_assert_int_eq(get_buf_offset(buf), 0);

	ck_assert_int_eq(buf_append_str(buf, "abc"), SLURM_SUCCESS);
	/* the NUL terminator must not be appended */
	ck_assert_int_eq(get_buf_offset(buf), 3);

	ck_assert_int_eq(buf_append_str(buf, "def"), SLURM_SUCCESS);
	ck_assert_int_eq(get_buf_offset(buf), 6);
	ck_assert_msg(!memcmp(get_buf_data(buf), "abcdef", 6),
		      "appended strings do not match the source");

	ck_assert_int_eq(buf_append_str(buf, ""), SLURM_SUCCESS);
	ck_assert_int_eq(get_buf_offset(buf), 6);

	free_buf(buf);
}

END_TEST

START_TEST(test_buf_append_str_grow)
{
	char *str = xmalloc(BUF_SIZE + 1);
	buf_t *buf = init_buf(16);

	memset(str, 'a', BUF_SIZE);

	ck_assert_int_eq(buf_append_str(buf, str), SLURM_SUCCESS);
	ck_assert_int_eq(get_buf_offset(buf), BUF_SIZE);
	ck_assert_int_eq(size_buf(buf), (16 + (2 * BUF_SIZE)));
	ck_assert_msg(!memcmp(get_buf_data(buf), str, BUF_SIZE),
		      "buf_append_str() did not preserve the contents");

	free_buf(buf);
	xfree(str);
}

END_TEST

START_TEST(test_assign_buf)
{
	char *data = xmalloc(64);
	buf_t *buf = init_buf(16);

	memcpy(data, "assigned", 8);

	assign_buf(buf, &data, 8);
	/* assign_buf() takes ownership of data */
	ck_assert(data == NULL);
	/* the buffer is sized by the allocation, not by the byte count */
	ck_assert_int_eq(size_buf(buf), 64);
	ck_assert_int_eq(get_buf_offset(buf), 8);
	ck_assert_int_eq(remaining_buf(buf), (64 - 8));
	ck_assert_msg(!memcmp(get_buf_data(buf), "assigned", 8),
		      "assign_buf() did not preserve the contents");

	free_buf(buf);
}

END_TEST

START_TEST(test_xfer_buf_data)
{
	buf_t *buf = init_buf(16);
	char *data;

	ck_assert_int_eq(buf_append_str(buf, "xfer"), SLURM_SUCCESS);

	data = xfer_buf_data(buf);
	/* the buf_t is released and the caller now owns the data */
	ck_assert(buf == NULL);
	ck_assert(data != NULL);
	ck_assert_msg(!memcmp(data, "xfer", 4),
		      "xfer_buf_data() did not preserve the contents");

	xfree(data);
}

END_TEST

static Suite *suite_buf(void)
{
	Suite *s = suite_create("buf");
	TCase *tc_core = tcase_create("buf");

	tcase_add_test(tc_core, test_init_buf);
	tcase_add_test(tc_core, test_try_init_buf);
	tcase_add_test(tc_core, test_create_buf);
	tcase_add_test(tc_core, test_create_shadow_buf);
	tcase_add_test(tc_core, test_create_mmap_buf);
	tcase_add_test(tc_core, test_shadow_buf_initializer);
	tcase_add_test(tc_core, test_buf_macros);
	tcase_add_test(tc_core, test_grow_buf);
	tcase_add_test(tc_core, test_try_grow_buf);
	tcase_add_test(tc_core, test_try_grow_buf_remaining);
	tcase_add_test(tc_core, test_buf_append_bytes);
	tcase_add_test(tc_core, test_buf_append_bytes_grow);
	tcase_add_test(tc_core, test_buf_append_bytes_too_large);
	tcase_add_test(tc_core, test_buf_append_str);
	tcase_add_test(tc_core, test_buf_append_str_grow);
	tcase_add_test(tc_core, test_assign_buf);
	tcase_add_test(tc_core, test_xfer_buf_data);

	suite_add_tcase(s, tc_core);
	return s;
}

int main(void)
{
	int number_failed;
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");
	SRunner *sr;

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);

	log_init("buf-test", log_opts, 0, NULL);

	sr = srunner_create(suite_buf());

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	log_fini();
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
