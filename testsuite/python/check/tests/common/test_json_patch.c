/*****************************************************************************\
 *  json_patch-test.c - Unit tests for the serializer/json-patch plugin
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
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/data.h"
#include "src/common/kv.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/serializer.h"

#include "scheck.h"

static void _setup(void)
{
	/* plugins load from the installed Slurm's default plugin dir */
	ck_assert(!serializer_g_init());
}

static void _teardown(void)
{
	serializer_g_fini();
}

static data_t *_from_json(const char *str)
{
	data_t *d = NULL;

	ck_assert(!serialize_g_string_to_data(&d, str, strlen(str),
					      MIME_TYPE_JSON));
	ck_assert(d);

	return d;
}

/* patch applies, result matches expected */
static void _check_apply(const char *target, const char *patch,
			 const char *expected)
{
	data_t *doc = _from_json(target);
	data_t *want = _from_json(expected);
	data_t *ops = _from_json(patch);
	int expect_rc = (data_get_list_length(ops) &&
			 data_check_match(doc, want, true)) ?
				SLURM_NO_CHANGE_IN_DATA :
				SLURM_SUCCESS;
	int rc = serialize_g_string_to_data(&doc, patch, strlen(patch),
					    MIME_TYPE_JSON_PATCH);

	ck_assert_msg(rc == expect_rc,
		      "patch %s on %s: rc %d (%s), expected %d (%s)", patch,
		      target, rc, slurm_strerror(rc), expect_rc,
		      slurm_strerror(expect_rc));
	ck_assert_msg(data_check_match(doc, want, true),
		      "patch %s on %s gave an unexpected result", patch,
		      target);

	FREE_NULL_DATA(ops);
	FREE_NULL_DATA(want);
	FREE_NULL_DATA(doc);
}

/* patch is rejected with expect_rc and the target is left unchanged */
static void _check_fail(const char *target, const char *patch, int expect_rc)
{
	data_t *doc = _from_json(target);
	data_t *before = _from_json(target);
	int rc = serialize_g_string_to_data(&doc, patch, strlen(patch),
					    MIME_TYPE_JSON_PATCH);

	ck_assert_msg(rc == expect_rc,
		      "patch %s on %s: rc %d (%s), expected %d (%s)", patch,
		      target, rc, slurm_strerror(rc), expect_rc,
		      slurm_strerror(expect_rc));

	/* RFC 6902 section 5: a failed patch changes nothing */
	ck_assert_msg(data_check_match(doc, before, true),
		      "failed patch %s on %s still changed the document", patch,
		      target);

	FREE_NULL_DATA(before);
	FREE_NULL_DATA(doc);
}

START_TEST(test_dict_ops)
{
	_check_apply("{\"a\":1,\"c\":3}",
		     "[{\"op\":\"add\",\"path\":\"/b\",\"value\":2},"
		     "{\"op\":\"replace\",\"path\":\"/a\",\"value\":10},"
		     "{\"op\":\"remove\",\"path\":\"/c\"}]",
		     "{\"a\":10,\"b\":2}");
}

END_TEST

START_TEST(test_list_ops)
{
	/* insert into the middle, append with "-", then remove the head */
	_check_apply("{\"l\":[1,3]}",
		     "[{\"op\":\"add\",\"path\":\"/l/1\",\"value\":2},"
		     "{\"op\":\"add\",\"path\":\"/l/-\",\"value\":4},"
		     "{\"op\":\"remove\",\"path\":\"/l/0\"}]",
		     "{\"l\":[2,3,4]}");
}

END_TEST

START_TEST(test_pointer_escapes)
{
	/* "~1" is '/' and "~0" is '~' */
	_check_apply("{\"a/b\":1,\"~x\":2}",
		     "[{\"op\":\"replace\",\"path\":\"/a~1b\",\"value\":9},"
		     "{\"op\":\"replace\",\"path\":\"/~0x\",\"value\":8}]",
		     "{\"a/b\":9,\"~x\":8}");
}

END_TEST

START_TEST(test_move_copy)
{
	_check_apply("{\"a\":{\"x\":1},\"b\":2}",
		     "[{\"op\":\"move\",\"from\":\"/a\",\"path\":\"/c\"},"
		     "{\"op\":\"copy\",\"from\":\"/b\",\"path\":\"/d\"}]",
		     "{\"b\":2,\"c\":{\"x\":1},\"d\":2}");
}

END_TEST

START_TEST(test_copy_same_array_index)
{
	_check_apply("[1,2]",
		     "[{\"op\":\"copy\",\"from\":\"/0\",\"path\":\"/0\"}]",
		     "[1,1,2]");
	_check_apply("[1,2]",
		     "[{\"op\":\"move\",\"from\":\"/0\",\"path\":\"/0\"}]",
		     "[1,2]");
	_check_apply("{\"a\":1}",
		     "[{\"op\":\"copy\",\"from\":\"/a\",\"path\":\"/a\"}]",
		     "{\"a\":1}");
}

END_TEST

START_TEST(test_noop_patch)
{
	/* empty operation lists succeed without SLURM_NO_CHANGE_IN_DATA */
	_check_apply("{\"a\":1}", "[]", "{\"a\":1}");
	_check_apply("{\"a\":1}", "[ \n ]", "{\"a\":1}");

	/* replacing a value with itself changes nothing */
	_check_apply("{\"a\":{\"b\":[1,2]}}",
		     "[{\"op\":\"replace\",\"path\":\"/a/b\",\"value\":[1,2]}]",
		     "{\"a\":{\"b\":[1,2]}}");

	/* an add undone by a remove nets no change */
	_check_apply("{\"a\":1}",
		     "[{\"op\":\"add\",\"path\":\"/b\",\"value\":2},"
		     "{\"op\":\"remove\",\"path\":\"/b\"}]",
		     "{\"a\":1}");

	/* a test-only patch changes nothing */
	_check_apply("{\"a\":1}",
		     "[{\"op\":\"test\",\"path\":\"/a\",\"value\":1}]",
		     "{\"a\":1}");

	/* a nonempty no-op does not replace the document's trees */
	{
		data_t *doc = _from_json("{\"a\":{\"b\":1}}");
		data_t *child = data_key_get(doc, "a");
		const char *patch =
			"[{\"op\":\"test\",\"path\":\"/a/b\",\"value\":1}]";
		int rc = serialize_g_string_to_data(&doc, patch, strlen(patch),
						    MIME_TYPE_JSON_PATCH);

		ck_assert_int_eq(rc, SLURM_NO_CHANGE_IN_DATA);
		ck_assert_ptr_eq(data_key_get(doc, "a"), child);

		FREE_NULL_DATA(doc);
	}
}

END_TEST

START_TEST(test_test_op)
{
	/* a matching test gates the replace */
	_check_apply("{\"a\":1}",
		     "[{\"op\":\"test\",\"path\":\"/a\",\"value\":1},"
		     "{\"op\":\"replace\",\"path\":\"/a\",\"value\":2}]",
		     "{\"a\":2}");

	/* an integer matches a float holding the same JSON number */
	_check_apply("{\"n\":2}",
		     "[{\"op\":\"test\",\"path\":\"/n\",\"value\":2.0}]",
		     "{\"n\":2}");

	/* a failed test fails the whole patch and changes nothing */
	_check_fail("{\"a\":1}",
		    "[{\"op\":\"replace\",\"path\":\"/a\",\"value\":9},"
		    "{\"op\":\"test\",\"path\":\"/a\",\"value\":1}]",
		    ESLURM_JSON_PATCH_TEST_FAILED);
}

END_TEST

START_TEST(test_operation_iteration)
{
	/* RFC 6902 sections 4.1 through 4.6, in order. */
	static const char *operations[] = {
		"{\"op\":\"add\",\"path\":\"/c\",\"value\":3}",
		"{\"op\":\"remove\",\"path\":\"/a\"}",
		"{\"op\":\"replace\",\"path\":\"/a\",\"value\":4}",
		"{\"op\":\"move\",\"from\":\"/a\",\"path\":\"/c\"}",
		"{\"op\":\"copy\",\"from\":\"/a\",\"path\":\"/c\"}",
		"{\"op\":\"test\",\"path\":\"/a\",\"value\":1}",
	};
	static const int errors[ARRAY_SIZE(operations)] = {
		SLURM_SUCCESS,
		ESLURM_DATA_PATH_NOT_FOUND,
		ESLURM_DATA_PATH_NOT_FOUND,
		ESLURM_DATA_PATH_NOT_FOUND,
		ESLURM_DATA_PATH_NOT_FOUND,
		ESLURM_JSON_PATCH_TEST_FAILED,
	};
	/* Rows are first operations; columns are second. NULL means rollback. */
	static const char *expected[][ARRAY_SIZE(operations)] = {
		/* add */
		{
			"{\"a\":1,\"b\":2,\"c\":3}",
			"{\"b\":2,\"c\":3}",
			"{\"a\":4,\"b\":2,\"c\":3}",
			"{\"b\":2,\"c\":1}",
			"{\"a\":1,\"b\":2,\"c\":1}",
			"{\"a\":1,\"b\":2,\"c\":3}",
		},
		/* remove */
		{ "{\"b\":2,\"c\":3}" },
		/* replace */
		{
			"{\"a\":4,\"b\":2,\"c\":3}",
			"{\"b\":2}",
			"{\"a\":4,\"b\":2}",
			"{\"b\":2,\"c\":4}",
			"{\"a\":4,\"b\":2,\"c\":4}",
		},
		/* move */
		{ "{\"b\":2,\"c\":3}" },
		/* copy */
		{
			"{\"a\":1,\"b\":2,\"c\":3}",
			"{\"b\":2,\"c\":1}",
			"{\"a\":4,\"b\":2,\"c\":1}",
			"{\"b\":2,\"c\":1}",
			"{\"a\":1,\"b\":2,\"c\":1}",
			"{\"a\":1,\"b\":2,\"c\":1}",
		},
		/* test */
		{
			"{\"a\":1,\"b\":2,\"c\":3}",
			"{\"b\":2}",
			"{\"a\":4,\"b\":2}",
			"{\"b\":2,\"c\":1}",
			"{\"a\":1,\"b\":2,\"c\":1}",
			"{\"a\":1,\"b\":2}",
		},
	};

	ck_assert_uint_eq(ARRAY_SIZE(expected), ARRAY_SIZE(operations));
	for (size_t i = 0; i < ARRAY_SIZE(operations); i++) {
		for (size_t j = 0; j < ARRAY_SIZE(operations); j++) {
			char *patch = xstrdup_printf("[%s,%s]", operations[i],
						     operations[j]);

			if (expected[i][j])
				_check_apply("{\"a\":1,\"b\":2}", patch,
					     expected[i][j]);
			else
				_check_fail("{\"a\":1,\"b\":2}", patch,
					    errors[j]);
			xfree(patch);
		}
	}

	_check_apply("{\"a\":1}", "[]", "{\"a\":1}");
	_check_apply("{\"a\":1}",
		     "[{\"op\":\"replace\",\"path\":\"/a\",\"value\":2},"
		     "{\"op\":\"test\",\"path\":\"/a\",\"value\":2},"
		     "{\"op\":\"replace\",\"path\":\"/a\",\"value\":3}]",
		     "{\"a\":3}");
	_check_fail("{\"a\":1}",
		    "[{\"op\":\"replace\",\"path\":\"/a\",\"value\":2},"
		    "{\"op\":\"remove\",\"path\":\"/missing\"},"
		    "{\"op\":\"bogus\",\"path\":\"/a\"}]",
		    ESLURM_DATA_PATH_NOT_FOUND);
}

END_TEST

START_TEST(test_root_pointer)
{
	/* "" addresses the whole document (RFC 6901 section 5) */
	_check_apply("{\"a\":1}",
		     "[{\"op\":\"add\",\"path\":\"\",\"value\":{\"b\":2}}]",
		     "{\"b\":2}");
	_check_apply("{\"a\":1}",
		     "[{\"op\":\"replace\",\"path\":\"\",\"value\":[1]}]",
		     "[1]");
	_check_apply("{\"a\":1}",
		     "[{\"op\":\"test\",\"path\":\"\",\"value\":{\"a\":1}}]",
		     "{\"a\":1}");
	_check_apply("{\"a\":1}",
		     "[{\"op\":\"copy\",\"from\":\"\",\"path\":\"/z\"}]",
		     "{\"a\":1,\"z\":{\"a\":1}}");

	/* removing the whole document is not possible */
	_check_fail("{\"a\":1}", "[{\"op\":\"remove\",\"path\":\"\"}]",
		    ESLURM_JSON_PATCH_FAILED);
}

END_TEST

START_TEST(test_invalid_patches)
{
	/* patch document must be an array of operations */
	_check_fail("{\"a\":1}", "{}", ESLURM_DATA_EXPECTED_LIST);

	/* unknown op */
	_check_fail("{\"a\":1}", "[{\"op\":\"bogus\",\"path\":\"/a\"}]",
		    ESLURM_JSON_PATCH_INVALID);

	/* path must be empty or begin with '/' */
	_check_fail("{\"a\":1}",
		    "[{\"op\":\"add\",\"path\":\"a\",\"value\":1}]",
		    ESLURM_JSON_POINTER_INVALID);

	/* removing a missing entry */
	_check_fail("{\"a\":1}", "[{\"op\":\"remove\",\"path\":\"/nope\"}]",
		    ESLURM_DATA_PATH_NOT_FOUND);
}

END_TEST

START_TEST(test_invalid_from)
{
	static const char *ops[] = { "move", "copy" };
	static const char *from[] = {
		"",
		",\"from\":null",
		",\"from\":1",
		",\"from\":true",
		",\"from\":{}",
		",\"from\":[]",
	};

	for (size_t i = 0; i < ARRAY_SIZE(ops); i++) {
		for (size_t j = 0; j < ARRAY_SIZE(from); j++) {
			char *patch = xstrdup_printf(
				"[{\"op\":\"%s\",\"path\":\"/b\"%s}]", ops[i],
				from[j]);

			_check_fail("{\"a\":1}", patch,
				    ESLURM_JSON_PATCH_INVALID);
			xfree(patch);
		}
	}
}

END_TEST

START_TEST(test_external_tool)
{
	const char *target = "{\"a\":1,\"l\":[1,2,3]}";
	const char *patch = "[{\"op\":\"replace\",\"path\":\"/a\",\"value\":2},"
			    "{\"op\":\"remove\",\"path\":\"/l/1\"}]";
	char *dir = NULL;
	char cmd[4096], buf[64 * 1024];
	data_t *doc, *want;
	FILE *fp;
	int rc;

	/* cross-check needs python3 with the jsonpatch module */
	if (system("python3 -c 'import jsonpatch' >/dev/null 2>&1")) {
		printf("%s: python3 jsonpatch module not found, skipping\n",
		       __func__);
		return;
	}

	doc = _from_json(target);
	rc = serialize_g_string_to_data(&doc, patch, strlen(patch),
					MIME_TYPE_JSON_PATCH);
	ck_assert_msg(!rc, "patch failed: %s", slurm_strerror(rc));

	/* apply the same patch with the external tool */
	ck_assert((dir = scheck_mkdtemp("json_patch")));
	scheck_write_file(dir, "target.json", target);
	scheck_write_file(dir, "patch.json", patch);

	snprintf(cmd, sizeof(cmd),
		 "python3 -c 'import jsonpatch, json, sys;"
		 " d = json.load(open(sys.argv[1]));"
		 " p = json.load(open(sys.argv[2]));"
		 " print(json.dumps(jsonpatch.apply_patch(d, p)))'"
		 " %s/target.json %s/patch.json",
		 dir, dir);
	ck_assert((fp = popen(cmd, "r")));
	ck_assert(fgets(buf, sizeof(buf), fp));
	ck_assert(!pclose(fp));

	want = _from_json(buf);
	ck_assert_msg(data_check_match(doc, want, true),
		      "plugin result differs from external tool: %s", buf);

	xfree(dir);
	FREE_NULL_DATA(want);
	FREE_NULL_DATA(doc);
}

END_TEST

START_TEST(test_openapi_mime_types)
{
	const char **all = get_mime_type_array(false);
	const char **openapi = get_mime_type_array(true);
	bool found = false;

	for (int i = 0; all[i]; i++) {
		if (!xstrcmp(all[i], MIME_TYPE_JSON_PATCH))
			found = true;
	}
	ck_assert_msg(found, "JSON Patch must remain registered");
	ck_assert_ptr_nonnull(resolve_mime_type(MIME_TYPE_JSON_PATCH, NULL));

	for (int i = 0; openapi[i]; i++) {
		ck_assert_str_ne(openapi[i], MIME_TYPE_JSON_PATCH);
		ck_assert_str_ne(openapi[i], MIME_TYPE_URL_ENCODED);
	}
}

END_TEST

START_TEST(test_no_patch_creation)
{
	data_t *doc = _from_json("{\"a\":1}");
	data_t *null_doc = NULL;
	char *out = NULL;
	size_t len = 0;
	const char *patch = "[{\"op\":\"remove\",\"path\":\"/a\"}]";
	int rc;

	/* creating a patch is not supported */
	rc = serialize_g_data_to_string(&out, &len, doc, MIME_TYPE_JSON_PATCH,
					SER_FLAGS_COMPACT);
	ck_assert_msg(rc == ESLURM_NOT_SUPPORTED,
		      "dump gave rc %d (%s), expected %d (%s)", rc,
		      slurm_strerror(rc), ESLURM_NOT_SUPPORTED,
		      slurm_strerror(ESLURM_NOT_SUPPORTED));
	ck_assert(!out);

	/* a patch needs the target document in dest */
	rc = serialize_g_string_to_data(&null_doc, patch, strlen(patch),
					MIME_TYPE_JSON_PATCH);
	ck_assert_msg(rc, "a NULL target must be rejected");
	ck_assert(!null_doc);

	FREE_NULL_DATA(doc);
}

END_TEST

START_TEST(test_kv_clear)
{
	data_t *src = (_i % 2) ? data_new() : NULL;
	data_t *dst = _from_json("{\"a\":1}");
	kv_op_t op = (_i / 2) ? KV_OP_PATCH : KV_OP_SET;

	ck_assert_int_eq(kv_op_apply(src, op, &dst, &op, NULL), SLURM_SUCCESS);
	ck_assert_ptr_null(dst);
	ck_assert_int_eq(op, KV_OP_NONE);

	FREE_NULL_DATA(src);
}

END_TEST

START_TEST(test_kv_patch_alias)
{
	data_t *kv =
		_from_json("[{\"op\":\"add\",\"path\":\"/a\",\"value\":1}]");
	kv_op_t op = KV_OP_PATCH;
	data_t *before;

	ck_assert_int_eq(kv_op_apply(kv, op, &kv, &op, NULL), SLURM_SUCCESS);
	ck_assert_int_eq(op, KV_OP_SET);
	ck_assert_int_eq(data_get_int(data_key_get(kv, "a")), 1);
	FREE_NULL_DATA(kv);

	kv = _from_json("[{\"op\":\"replace\",\"path\":\"\",\"value\":7}]");
	op = KV_OP_PATCH;
	before = kv;
	ck_assert_int_eq(kv_op_apply(kv, op, &kv, &op, NULL),
			 ESLURM_KV_NOT_DICT);
	ck_assert_ptr_eq(kv, before);
	ck_assert_int_eq(op, KV_OP_PATCH);
	FREE_NULL_DATA(kv);
}

END_TEST

START_TEST(test_kv_result_operation)
{
	data_t *dst = NULL, *patch = _from_json("[]"), *before;
	kv_op_t op = KV_OP_PATCH;

	ck_assert_int_eq(kv_op_apply(patch, op, &dst, &op, NULL),
			 SLURM_SUCCESS);
	ck_assert_ptr_null(dst);
	ck_assert_int_eq(op, KV_OP_NONE);
	FREE_NULL_DATA(patch);

	patch = _from_json("[{\"op\":\"add\",\"path\":\"/a\",\"value\":1}]");
	op = KV_OP_PATCH;
	ck_assert_int_eq(kv_op_apply(patch, op, &dst, &op, NULL),
			 SLURM_SUCCESS);
	ck_assert_int_eq(op, KV_OP_SET);
	ck_assert_int_eq(data_get_int(data_key_get(dst, "a")), 1);
	FREE_NULL_DATA(patch);

	/* Source and destination document and operation may alias. */
	ck_assert_int_eq(kv_op_apply(dst, op, &dst, &op, NULL), SLURM_SUCCESS);
	ck_assert_int_eq(op, KV_OP_SET);
	ck_assert_int_eq(data_get_int(data_key_get(dst, "a")), 1);

	patch = _from_json("[{\"op\":\"test\",\"path\":\"/a\",\"value\":1}]");
	op = KV_OP_PATCH;
	ck_assert_int_eq(kv_op_apply(patch, op, &dst, &op, NULL),
			 SLURM_NO_CHANGE_IN_DATA);
	ck_assert_int_eq(op, KV_OP_SET);
	FREE_NULL_DATA(patch);

	before = dst;
	ck_assert_int_eq(kv_op_apply(NULL, KV_OP_NONE, &dst, &op, NULL),
			 SLURM_SUCCESS);
	ck_assert_ptr_eq(dst, before);
	ck_assert_int_eq(op, KV_OP_SET);

	patch = _from_json("[{\"op\":\"replace\",\"path\":\"\",\"value\":7}]");
	op = KV_OP_PATCH;
	ck_assert_int_eq(kv_op_apply(patch, op, &dst, &op, NULL),
			 ESLURM_KV_NOT_DICT);
	ck_assert_ptr_eq(dst, before);
	ck_assert_int_eq(op, KV_OP_PATCH);
	FREE_NULL_DATA(patch);

	patch = _from_json("[{\"op\":\"remove\",\"path\":\"/a\"}]");
	ck_assert_int_eq(kv_op_apply(patch, op, &dst, &op, NULL),
			 SLURM_SUCCESS);
	ck_assert_ptr_null(dst);
	ck_assert_int_eq(op, KV_OP_NONE);
	FREE_NULL_DATA(patch);

	dst = _from_json("{\"a\":1}");
	ck_assert_int_eq(kv_op_apply(NULL, KV_OP_PATCH, &dst, NULL, NULL),
			 SLURM_SUCCESS);
	ck_assert_ptr_null(dst);
}

END_TEST

START_TEST(test_kv_error_messages)
{
	const char *notice = "job_submit notice";
	char *err_msg = xstrdup(notice);
	data_t *src = NULL, *dst = NULL;

	ck_assert_int_eq(kv_op_apply(NULL, KV_OP_NONE, &dst, NULL, &err_msg),
			 SLURM_SUCCESS);
	ck_assert_str_eq(err_msg, notice);

	ck_assert_int_eq(kv_parse(KV_OP_SET, "[]", &src, &err_msg),
			 ESLURM_KV_NOT_DICT);
	ck_assert_ptr_null(src);
	ck_assert_ptr_eq(xstrstr(err_msg, notice), err_msg);
	ck_assert_ptr_nonnull(xstrstr(err_msg, "\nKV must be a JSON object"));
	xfree(err_msg);

	err_msg = xstrdup(notice);
	src = data_set_list(data_new());
	ck_assert_int_eq(kv_op_apply(src, KV_OP_SET, &dst, NULL, &err_msg),
			 ESLURM_KV_NOT_DICT);
	ck_assert_str_eq(err_msg,
			 "job_submit notice\nKV must be a JSON object");
	FREE_NULL_DATA(src);
	xfree(err_msg);

	err_msg = xstrdup(notice);
	dst = _from_json("{\"a\":1}");
	src = _from_json("[{\"op\":\"test\",\"path\":\"/a\",\"value\":1}]");
	ck_assert_int_eq(kv_op_apply(src, KV_OP_PATCH, &dst, NULL, &err_msg),
			 SLURM_NO_CHANGE_IN_DATA);
	ck_assert_str_eq(err_msg, notice);
	FREE_NULL_DATA(src);

	src = _from_json("[{\"op\":\"remove\",\"path\":\"/missing\"}]");
	ck_assert_int_eq(kv_op_apply(src, KV_OP_PATCH, &dst, NULL, &err_msg),
			 ESLURM_DATA_PATH_NOT_FOUND);
	ck_assert_ptr_eq(xstrstr(err_msg, notice), err_msg);
	ck_assert_ptr_nonnull(xstrstr(err_msg, "\nUnable to apply KVPatch:"));
	FREE_NULL_DATA(src);
	FREE_NULL_DATA(dst);
	xfree(err_msg);
}

END_TEST

static Suite *_suite_json_patch(void)
{
	Suite *s = suite_create("json-patch");
	TCase *tc_core = tcase_create("json-patch");

	tcase_add_checked_fixture(tc_core, _setup, _teardown);
	tcase_add_test(tc_core, test_dict_ops);
	tcase_add_test(tc_core, test_list_ops);
	tcase_add_test(tc_core, test_pointer_escapes);
	tcase_add_test(tc_core, test_root_pointer);
	tcase_add_test(tc_core, test_move_copy);
	tcase_add_test(tc_core, test_copy_same_array_index);
	tcase_add_test(tc_core, test_noop_patch);
	tcase_add_test(tc_core, test_test_op);
	tcase_add_test(tc_core, test_operation_iteration);
	tcase_add_test(tc_core, test_invalid_patches);
	tcase_add_test(tc_core, test_invalid_from);
	tcase_add_test(tc_core, test_external_tool);
	tcase_add_test(tc_core, test_openapi_mime_types);
	tcase_add_test(tc_core, test_no_patch_creation);
	tcase_add_loop_test(tc_core, test_kv_clear, 0, 4);
	tcase_add_test(tc_core, test_kv_patch_alias);
	tcase_add_test(tc_core, test_kv_result_operation);
	tcase_add_test(tc_core, test_kv_error_messages);
	suite_add_tcase(s, tc_core);

	return s;
}

int main(void)
{
	int number_failed;
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);

	log_init("json-patch-test", log_opts, 0, NULL);

	SRunner *sr = srunner_create(_suite_json_patch());

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	log_fini();
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
