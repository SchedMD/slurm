/*****************************************************************************\
 *  Copyright (C) SchedMD LLC.
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
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define check_with_data_get_bool_converted(str, b)                          \
	do {                                                                \
		bool bres;                                                  \
		int rc;                                                     \
		data_set_string(d, str);                                    \
		rc = data_get_bool_converted(d, &bres);                     \
		ck_assert_msg(rc == 0,                                      \
			      "bool convert string:%s->%s rc:%s [%d]",      \
			      str ? str : "(null)", (b ? "true" : "false"),	\
			      slurm_strerror(rc), rc);                      \
		if (!rc)                                                    \
			ck_assert_msg(bres == b,                            \
				      "bool converted: %s -> %s == %s",     \
				      str ? str : "(null)", (bres ? "true" : "false"), \
				      (b ? "true" : "false"));              \
	} while (0)

/* Detected type of str must be a 64 bit integer holding value */
#define check_convert_int(str, value) \
	do { \
		data_t *d = data_set_string(data_new(), str); \
		data_type_t type = data_convert_type(d, DATA_TYPE_NONE); \
		ck_assert_msg(type == DATA_TYPE_INT_64, \
			      "convert \"%s\" -> %s, expected %s", str, \
			      data_type_to_string(type), \
			      data_type_to_string(DATA_TYPE_INT_64)); \
		ck_assert_msg(data_get_int(d) == (value), \
			      "convert \"%s\" -> %" PRId64 \
			      ", expected %" PRId64, \
			      str, data_get_int(d), (int64_t) (value)); \
		FREE_NULL_DATA(d); \
	} while (0)

/* Detected type of str must be exactly expected */
#define check_convert_type(str, expected) \
	do { \
		data_t *d = data_set_string(data_new(), str); \
		data_type_t type = data_convert_type(d, DATA_TYPE_NONE); \
		ck_assert_msg(type == (expected), \
			      "convert \"%s\" -> %s, expected %s", str, \
			      data_type_to_string(type), \
			      data_type_to_string(expected)); \
		FREE_NULL_DATA(d); \
	} while (0)

/* Forced conversion of str must be a 64 bit integer holding value */
#define check_convert_forced_int(str, value) \
	do { \
		data_t *d = data_set_string(data_new(), str); \
		data_type_t type = data_convert_type(d, DATA_TYPE_INT_64); \
		ck_assert_msg(type == DATA_TYPE_INT_64, \
			      "force convert \"%s\" -> %s, expected %s", str, \
			      data_type_to_string(type), \
			      data_type_to_string(DATA_TYPE_INT_64)); \
		ck_assert_msg(data_get_int(d) == (value), \
			      "force convert \"%s\" -> %" PRId64 \
			      ", expected %" PRId64, \
			      str, data_get_int(d), (int64_t) (value)); \
		FREE_NULL_DATA(d); \
	} while (0)

/* Forced conversion of str must refuse to produce a 64 bit integer */
#define check_convert_forced_not_int(str) \
	do { \
		data_t *d = data_set_string(data_new(), str); \
		data_type_t type = data_convert_type(d, DATA_TYPE_INT_64); \
		ck_assert_msg( \
			type != DATA_TYPE_INT_64, \
			"force convert \"%s\" -> %s, expected any other type", \
			str, data_type_to_string(type)); \
		FREE_NULL_DATA(d); \
	} while (0)

/* Forced conversion of float value must be a 64 bit integer expected */
#define check_convert_float_to_int(value, expected) \
	do { \
		data_t *d = data_set_float(data_new(), value); \
		data_type_t type = data_convert_type(d, DATA_TYPE_INT_64); \
		ck_assert_msg(type == DATA_TYPE_INT_64, \
			      "force convert float %f -> %s, expected %s", \
			      (double) (value), data_type_to_string(type), \
			      data_type_to_string(DATA_TYPE_INT_64)); \
		ck_assert_msg(data_get_int(d) == (expected), \
			      "force convert float %f -> %" PRId64 \
			      ", expected %" PRId64, \
			      (double) (value), data_get_int(d), \
			      (int64_t) (expected)); \
		FREE_NULL_DATA(d); \
	} while (0)

/* Forced conversion of float value must refuse to produce an integer */
#define check_convert_float_not_int(value) \
	do { \
		data_t *d = data_set_float(data_new(), value); \
		data_type_t type = data_convert_type(d, DATA_TYPE_INT_64); \
		ck_assert_msg( \
			type != DATA_TYPE_INT_64, \
			"force convert float %f -> %s, expected any other type", \
			(double) (value), data_type_to_string(type)); \
		FREE_NULL_DATA(d); \
	} while (0)

/* Detected type of str must be anything but a 64 bit integer */
#define check_convert_not_int(str) \
	do { \
		data_t *d = data_set_string(data_new(), str); \
		data_type_t type = data_convert_type(d, DATA_TYPE_NONE); \
		ck_assert_msg(type != DATA_TYPE_INT_64, \
			      "convert \"%s\" -> %s, expected any other type", \
			      str, data_type_to_string(type)); \
		FREE_NULL_DATA(d); \
	} while (0)

/*
 * Comparing floats x and y must give expected. The answer must not depend on
 * the order of the arguments, and mask only applies to dictionaries and lists
 * so it must not change a float comparison either.
 */
#define check_float_match(x, y, expected) \
	do { \
		data_t *da = data_set_float(data_new(), (x)); \
		data_t *db = data_set_float(data_new(), (y)); \
		bool ab = data_check_match(da, db, false); \
		bool ba = data_check_match(db, da, false); \
		bool masked = data_check_match(da, db, true); \
		ck_assert_msg(ab == (expected), \
			      "match %e, %e -> %s, expected %s", (double) (x), \
			      (double) (y), (ab ? "true" : "false"), \
			      ((expected) ? "true" : "false")); \
		ck_assert_msg(ba == ab, \
			      "match %e, %e -> %s, but reversed -> %s", \
			      (double) (x), (double) (y), \
			      (ab ? "true" : "false"), \
			      (ba ? "true" : "false")); \
		ck_assert_msg(masked == ab, \
			      "match %e, %e -> %s, but masked -> %s", \
			      (double) (x), (double) (y), \
			      (ab ? "true" : "false"), \
			      (masked ? "true" : "false")); \
		FREE_NULL_DATA(da); \
		FREE_NULL_DATA(db); \
	} while (0)

static data_for_each_cmd_t
	_find_dict_bool(const char *key, const data_t *data, void *arg)
{
	int *found = arg;

	ck_assert_msg(data_get_type(data) == DATA_TYPE_BOOL, "entry bool type");

	if (data_get_bool(data))
		(*found)++;

	ck_assert(key != NULL);
	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t
	_invert_dict_bool(const char *key, data_t *data, void *arg)
{
	ck_assert_msg(data_get_type(data) == DATA_TYPE_BOOL, "entry bool type");
	ck_assert(key != NULL);
	data_set_bool(data, !data_get_bool(data));
	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t
	_del_dict_bool_true(const char *key, data_t *data, void *arg)
{
	int *max = arg;

	ck_assert(key != NULL);
	ck_assert_msg(data_get_type(data) == DATA_TYPE_BOOL, "entry bool type");

	if (*max <= 0)
		return DATA_FOR_EACH_STOP;

	if (data_get_bool(data)) {
		*max -= 1;
		return DATA_FOR_EACH_DELETE;
	}

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _del_list_odd(data_t *data, void *arg)
{
	int *max = arg;

	ck_assert_msg(data_get_type(data) == DATA_TYPE_INT_64,
		      "entry int type");

	if (*max <= 0)
		return DATA_FOR_EACH_STOP;

	if (data_get_int(data) % 2 == 1) {
		*max -= 1;
		return DATA_FOR_EACH_DELETE;
	}

	return DATA_FOR_EACH_CONT;
}

data_for_each_cmd_t _check_list_order(const data_t *data, void *arg)
{
	int *found = arg;

	ck_assert_msg(data_get_int(data) == *found,
		      "check value");

	*found += 1;
	return DATA_FOR_EACH_CONT;
}


START_TEST(test_list_iteration)
{
	int max;
	int found = 0;
	data_t *d = data_new();
	data_set_list(d);

	ck_assert_msg(data_get_type(d) == DATA_TYPE_LIST, "check list type");

	data_set_int(data_list_append(d), 5);
	data_set_int(data_list_prepend(d), 4);
	data_set_int(data_list_append(d), 6);
	data_set_int(data_list_prepend(d), 3);
	data_set_int(data_list_append(d), 7);
	data_set_int(data_list_prepend(d), 2);
	data_set_int(data_list_append(d), 8);
	data_set_int(data_list_prepend(d), 1);
	data_set_int(data_list_append(d), 9);
	data_set_int(data_list_prepend(d), 0);

	ck_assert_msg(data_get_type(d) == DATA_TYPE_LIST, "check list type");
	ck_assert_msg(data_get_list_length(d) == 10, "list count");

	found = 0;
	ck_assert_msg(data_list_for_each_const(d, _check_list_order, &found) ==
		      10, "order touch count");
	ck_assert_msg(found == 10, "check max found");

	data_set_int(data_list_append(d), 10);

	found = 0;
	ck_assert_msg(data_list_for_each_const(d, _check_list_order, &found) ==
		      11, "order touch count");
	ck_assert_msg(found == 11, "check max found");

	max = 1;
	data_list_for_each(d, _del_list_odd, &max);
	ck_assert_msg(data_get_list_length(d) == 10, "list count");
	ck_assert_msg(max == 0, "check remove count");

	max = 20;
	data_list_for_each(d, _del_list_odd, &max);
	ck_assert_msg(data_get_list_length(d) == 6, "list count");
	ck_assert_msg(max == 16, "check remove count");

	FREE_NULL_DATA(d);
}
END_TEST

START_TEST(test_dict_iteration)
{
	int max;
	int found = 0;
	data_t *d = data_new();
	data_set_dict(d);

	data_set_bool(data_key_set(d, "true1"), true);
	data_set_bool(data_key_set(d, "true2"), true);
	data_set_bool(data_key_set(d, "true3"), true);
	data_set_bool(data_key_set(d, "true4"), true);
	data_set_bool(data_key_set(d, "true5"), true);
	data_set_bool(data_key_set(d, "false1"), false);
	data_set_bool(data_key_set(d, "false2"), false);
	data_set_bool(data_key_set(d, "false3"), false);
	data_set_bool(data_key_set(d, "false4"), false);
	data_set_bool(data_key_set(d, "false5"), false);
	ck_assert_msg(data_get_dict_length(d) == 10, "dict cardinality");
	ck_assert_msg(data_dict_for_each_const(d, _find_dict_bool, &found) ==
		      10, "find true");
	ck_assert_msg(found == 5, "found true");

	ck_assert_msg(data_dict_for_each(d, _invert_dict_bool, NULL) == 10,
		      "invert true");
	ck_assert_msg(data_get_dict_length(d) == 10, "dict cardinality");
	found = 0;
	ck_assert_msg(data_dict_for_each_const(d, _find_dict_bool, &found) ==
		      10, "find true");
	ck_assert_msg(found == 5, "found true");

	max = 1;
	data_dict_for_each(d, _del_dict_bool_true, &max);
	ck_assert_msg(max == 0, "remove 1 true");

	found = 0;
	ck_assert_msg(data_dict_for_each_const(d, _find_dict_bool, &found) == 9,
		      "find true");
	ck_assert_msg(found == 4, "found true");
	ck_assert_msg(data_get_dict_length(d) == 9, "dict cardinality");

	max = 0;
	data_dict_for_each(d, _del_dict_bool_true, &max);
	ck_assert_msg(max == 0, "no op remove");
	ck_assert_msg(data_get_dict_length(d) == 9,
		      "dict cardinality after no op");

	max = 4;
	data_dict_for_each(d, _del_dict_bool_true, &max);
	ck_assert_msg(max == 0, "remove all true");
	ck_assert_msg(data_get_dict_length(d) == 5, "dict cardinality");

	FREE_NULL_DATA(d);
}
END_TEST

START_TEST(test_dict_typeset)
{
	data_t *d = data_new();

	ck_assert_msg(data_get_type(d) == DATA_TYPE_NULL, "default type");
	data_set_dict(d);
	ck_assert_msg(data_get_type(d) == DATA_TYPE_DICT, "dict type");
	ck_assert_msg(data_get_dict_length(d) == 0, "dict cardinality");
	data_key_set(d, "test1");
	data_key_set(d, "test2");
	data_key_set(d, "test3");
	data_key_set(d, "test4");
	data_key_set(d, "test5");
	ck_assert_msg(data_get_dict_length(d) == 5, "dict cardinality");

	data_set_list(d);
	ck_assert_msg(data_get_type(d) == DATA_TYPE_LIST, "list type");
	ck_assert_msg(data_get_list_length(d) == 0, "list cardinality");
	data_list_append(d);
	data_list_prepend(d);
	data_list_prepend(d);
	data_list_append(d);
	data_list_append(d);
	ck_assert_msg(data_get_list_length(d) == 5, "list cardinality");

	data_set_int(d, 100);
	ck_assert_msg(data_get_type(d) == DATA_TYPE_INT_64, "int type");
	ck_assert_msg(data_get_int(d) == 100, "check int value");

	char *str = NULL;
	ck_assert_msg(data_get_string_converted(d, &str) == 0,
		      "convert 100 to string");
	ck_assert_msg(xstrcmp(str, "100") == 0, "check 100 got converted");
	xfree(str);

	ck_assert_msg(data_convert_type(d, DATA_TYPE_STRING) ==
		      DATA_TYPE_STRING, "convert 100 to string");
	ck_assert_msg(data_get_type(d) == DATA_TYPE_STRING, "int type");
	ck_assert_msg(xstrcmp(data_get_string(d), "100") == 0,
		      "check 100 got converted");

	int64_t b = 0;
	ck_assert_msg(data_get_int_converted(d, &b) == 0,
		      "convert 100 from string");
	ck_assert_msg(data_get_type(d) == DATA_TYPE_STRING,
		      "check still string type");
	ck_assert_msg(b == 100, "check string conversion from 100");

	ck_assert_msg(data_convert_type(d, DATA_TYPE_INT_64) ==
		      DATA_TYPE_INT_64, "convert 100 from string");
	ck_assert_msg(data_get_type(d) == DATA_TYPE_INT_64, "int type");
	ck_assert_msg(data_get_int(d) == 100,
		      "check string conversion from 100");

	data_set_float(d, 3.14);
	ck_assert_msg(data_get_type(d) == DATA_TYPE_FLOAT, "float type");

	str = NULL;
	ck_assert_msg(data_get_string_converted(d, &str) == 0,
		      "convert 3.14 to string");
	ck_assert_msg(xstrcmp(str, "3.140000") == 0,
		      "check 3.14 got converted");
	xfree(str);
	ck_assert_msg(data_get_type(d) == DATA_TYPE_FLOAT, "float type");

	ck_assert_msg(data_convert_type(d, DATA_TYPE_FLOAT) == DATA_TYPE_FLOAT,
		      "convert 100 from string");
	ck_assert_msg(data_get_type(d) == DATA_TYPE_FLOAT, "int type");
	ck_assert_msg(data_get_float(d) == 3.14,
		      "check string conversion from 3.14");

	data_set_float(d, -3.14);
	ck_assert_msg(data_get_type(d) == DATA_TYPE_FLOAT, "float type");

	str = NULL;
	ck_assert_msg(data_get_string_converted(d, &str) == 0,
		      "convert -3.14 to string");
	ck_assert_msg(xstrcmp(str, "-3.140000") == 0,
		      "check -3.14 got converted");
	xfree(str);
	ck_assert_msg(data_get_type(d) == DATA_TYPE_FLOAT, "float type");
	ck_assert_msg(data_get_float(d) == -3.14,
		      "check string conversion from -3.14");

	data_set_null(d);
	ck_assert_msg(data_get_type(d) == DATA_TYPE_NULL, "default type");

	FREE_NULL_DATA(d);
	ck_assert_msg(d == NULL, "free check");
}
END_TEST

static data_for_each_cmd_t _list_is_index(const data_t *data, void *arg)
{
	int *i_ptr = arg;
	int64_t v;

	ck_assert(!data_get_int_converted(data, &v));
	ck_assert_int_eq(v, *i_ptr);

	(*i_ptr)++;

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _dict_is_index(const char *key, const data_t *data,
					  void *arg)
{
	int *i_ptr = arg;
	int64_t v;
	data_t *d = data_set_string(data_new(), key);

	ck_assert(data_convert_type(d, DATA_TYPE_INT_64) == DATA_TYPE_INT_64);
	ck_assert(data_get_int(d) == *i_ptr);
	ck_assert(!data_get_int_converted(data, &v));
	ck_assert_int_eq(v, *i_ptr);

	(*i_ptr)++;

	return DATA_FOR_EACH_CONT;
}

START_TEST(test_convert_list_dict)
{
	static const int c = 10;
	data_t *d = data_set_dict(data_new());

	for (int i = 0; i < c; i++)
		data_set_string_fmt(data_key_set_int(d, i), "%d", i);

	for (int i = 0; i < c; i++) {
		int64_t v;

		ck_assert(!data_get_int_converted(data_key_get_int(d, i), &v));
		ck_assert_int_eq(v, i);
	}

	ck_assert(data_convert_type(d, DATA_TYPE_LIST) == DATA_TYPE_LIST);
	ck_assert(data_get_type(d) == DATA_TYPE_LIST);

	{
		int i = 0;
		ck_assert(data_list_for_each_const(d, _list_is_index, &i) == c);
		ck_assert_int_eq(i, c);
	}

	ck_assert(data_convert_type(d, DATA_TYPE_DICT) == DATA_TYPE_DICT);
	ck_assert(data_get_type(d) == DATA_TYPE_DICT);

	{
		int i = 0;
		ck_assert(data_dict_for_each_const(d, _dict_is_index, &i) == c);
		ck_assert_int_eq(i, c);
	}

	FREE_NULL_DATA(d);
}

END_TEST

START_TEST(test_data_move)
{
	data_t *src, *dst;

	/* NULL destination is allocated by data_move() */
	src = data_set_int(data_new(), 42);
	dst = data_move(NULL, src);
	ck_assert(dst != NULL);
	ck_assert(data_get_type(dst) == DATA_TYPE_INT_64);
	ck_assert_int_eq(data_get_int(dst), 42);
	ck_assert(data_get_type(src) == DATA_TYPE_NULL);
	FREE_NULL_DATA(dst);
	FREE_NULL_DATA(src);

	/* value transfers to the destination and empties the source */
	src = data_set_string(data_new(), "tacos");
	dst = data_new();
	ck_assert(data_move(dst, src) == dst);
	ck_assert(data_get_type(dst) == DATA_TYPE_STRING);
	ck_assert_str_eq(data_get_string(dst), "tacos");
	ck_assert(data_get_type(src) == DATA_TYPE_NULL);
	FREE_NULL_DATA(dst);
	FREE_NULL_DATA(src);

	/* an entire subtree moves in a single call */
	src = data_set_list(data_new());
	data_set_string(data_list_append(src), "taco1");
	data_set_string(data_list_append(src), "taco2");
	dst = data_new();
	ck_assert(data_move(dst, src) == dst);
	ck_assert(data_get_type(dst) == DATA_TYPE_LIST);
	ck_assert_int_eq(data_get_list_length(dst), 2);
	ck_assert(data_get_type(src) == DATA_TYPE_NULL);
	FREE_NULL_DATA(dst);
	FREE_NULL_DATA(src);

	/*
	 * A populated scalar destination is replaced. The string is longer
	 * than the inline string buffer so that releasing the destination has
	 * to free a heap allocation, which is the leak being fixed.
	 */
	dst = data_set_string(data_new(), "taco truck");
	src = data_set_bool(data_new(), true);
	ck_assert(data_move(dst, src) == dst);
	ck_assert(data_get_type(dst) == DATA_TYPE_BOOL);
	ck_assert(data_get_bool(dst));
	FREE_NULL_DATA(dst);
	FREE_NULL_DATA(src);

	/* a populated list destination is released and replaced */
	dst = data_set_list(data_new());
	data_set_string(data_list_append(dst), "taco1");
	data_set_string(data_list_append(dst), "taco2");
	src = data_set_string(data_new(), "tacos");
	ck_assert(data_move(dst, src) == dst);
	ck_assert(data_get_type(dst) == DATA_TYPE_STRING);
	ck_assert_str_eq(data_get_string(dst), "tacos");
	FREE_NULL_DATA(dst);
	FREE_NULL_DATA(src);

	/* a populated dictionary destination is released and replaced */
	dst = data_set_dict(data_new());
	data_set_string(data_key_set(dst, "taco"), "tacos");
	src = data_set_int(data_new(), -1);
	ck_assert(data_move(dst, src) == dst);
	ck_assert(data_get_type(dst) == DATA_TYPE_INT_64);
	ck_assert_int_eq(data_get_int(dst), -1);
	FREE_NULL_DATA(dst);
	FREE_NULL_DATA(src);

	/* moving onto an existing key only replaces that entry */
	dst = data_set_dict(data_new());
	data_set_string(data_key_set(dst, "taco1"), "tacos");
	data_set_list(data_key_set(dst, "taco2"));
	data_set_string(data_list_append(data_key_get(dst, "taco2")), "tacos");
	src = data_set_string(data_new(), "burrito");
	ck_assert(data_move(data_key_set(dst, "taco2"), src) != NULL);
	ck_assert_int_eq(data_get_dict_length(dst), 2);
	ck_assert(data_get_type(data_key_get(dst, "taco2")) ==
		  DATA_TYPE_STRING);
	ck_assert_str_eq(data_get_string(data_key_get(dst, "taco2")),
			 "burrito");
	ck_assert_str_eq(data_get_string(data_key_get(dst, "taco1")), "tacos");
	ck_assert(data_get_type(src) == DATA_TYPE_NULL);
	FREE_NULL_DATA(dst);
	FREE_NULL_DATA(src);
}

END_TEST

START_TEST(test_detection)
{
	data_t *d = data_new();

	check_with_data_get_bool_converted("1", true);
	check_with_data_get_bool_converted("100", true);
	check_with_data_get_bool_converted("-100", true);
	check_with_data_get_bool_converted("true", true);
	check_with_data_get_bool_converted("taco", true);
	check_with_data_get_bool_converted("0", false);
	check_with_data_get_bool_converted("false", false);
	check_with_data_get_bool_converted("-0", false);
	check_with_data_get_bool_converted(NULL, false);

	FREE_NULL_DATA(d);
}
END_TEST

#ifndef NDEBUG
START_TEST(test_data_move_null_src)
{
	data_t *dst = data_set_string(data_new(), "taco");

	/* expect SIGABRT */
	/* src must never be NULL */
	data_move(dst, NULL);
}

END_TEST
#endif /* !NDEBUG */

#ifndef NDEBUG
START_TEST(test_data_move_self)
{
	data_t *d = data_set_string(data_new(), "taco");

	/* expect SIGABRT */
	/*
	 * dest must never be src: _release(dest) runs before the contents are
	 * copied, so a self move would free what it is about to read.
	 */
	data_move(d, d);
}

END_TEST
#endif /* !NDEBUG */

START_TEST(test_convert_int)
{
	/*
	 * Negative integers must detect as integers and not fall through to
	 * the float converter, which silently loses precision beyond 2^53.
	 */
	check_convert_int("-1", -1);
	check_convert_int("-100", -100);
	check_convert_int("-0", 0);
	check_convert_int("-1234567890123456789", -1234567890123456789LL);
	check_convert_int("-9223372036854775807", -9223372036854775807LL);

	/* Positive integers must keep working */
	check_convert_int("1", 1);
	check_convert_int("100", 100);
	check_convert_int("1234567890123456789", 1234567890123456789LL);
	check_convert_int("0", 0);
	check_convert_int("9223372036854775807", 9223372036854775807LL);

	/* The int64_t limits themselves must convert exactly */
	check_convert_int("-9223372036854775808", INT64_MIN);
	check_convert_int("9223372036854775807", INT64_MAX);
	check_convert_int("+9223372036854775807", INT64_MAX);

	/*
	 * One past either limit must never be clamped into an integer:
	 * reporting a successful conversion to a different number is worse
	 * than not converting at all. Auto detection has no integer left to
	 * detect, so the value goes on to the float converter rather than
	 * being rejected outright.
	 */
	check_convert_type("-9223372036854775809", DATA_TYPE_FLOAT);
	check_convert_type("9223372036854775808", DATA_TYPE_FLOAT);
	check_convert_type("+9223372036854775808", DATA_TYPE_FLOAT);

	/*
	 * A forced conversion is where the rejection is observable, since
	 * there is no float converter left to fall through to.
	 */
	check_convert_forced_int("42", 42);
	check_convert_forced_int("  42", 42);
	check_convert_forced_int("-9223372036854775808", INT64_MIN);
	check_convert_forced_int("9223372036854775807", INT64_MAX);
	check_convert_forced_not_int("-9223372036854775809");
	check_convert_forced_not_int("9223372036854775808");
	check_convert_forced_not_int("1a");

	/*
	 * A float outside the integer range must be refused too. lrint() is
	 * undefined there and returns INT64_MIN, which consumers that store
	 * into an unsigned field truncate to 0 - PARSE_FUNC(USER_ID) would
	 * turn "9223372036854775808" into uid 0 rather than rejecting it.
	 */
	check_convert_float_not_int(9223372036854775808.0);
	check_convert_float_not_int(-9223372036854777856.0);
	check_convert_float_not_int(INFINITY);
	check_convert_float_not_int(-INFINITY);
	check_convert_float_not_int(NAN);

	/* In range floats must still convert, rounding to nearest */
	check_convert_float_to_int(1.5, 2);
	check_convert_float_to_int(-1.5, -2);
	check_convert_float_to_int(-9223372036854775808.0, INT64_MIN);
	check_convert_float_to_int(9223372036854774784.0,
				   9223372036854774784LL);

	/*
	 * A hex literal is a bit pattern, not a signed value: the whole
	 * unsigned 64 bit range is accepted and reinterpreted as int64_t, so
	 * anything above INT64_MAX reads back negative. The sentinels are
	 * spelled this way, so these must keep converting to the same numbers
	 * they always did.
	 */
	check_convert_int("0x0", 0);
	check_convert_int("0x10", 16);
	check_convert_int("0X10", 16);
	check_convert_int("0x7FFFFFFFFFFFFFFF", INT64_MAX);
	check_convert_int("0x8000000000000000", INT64_MIN);
	check_convert_int("0xfffffffffffffffe", -2);
	check_convert_int("0xFFFFFFFFFFFFFFFF", -1);

	/*
	 * Only a literal too wide for 64 bits is refused as an integer.
	 * sscanf() saturated these to UINT64_MAX and reported a successful
	 * conversion, so both used to read back as -1. Auto detection then
	 * hands them to the float converter, whose "%lf" accepts C99 hex
	 * float notation, so they land on float the same way an out of range
	 * decimal does rather than staying a string.
	 */
	check_convert_type("0x10000000000000000", DATA_TYPE_FLOAT);
	check_convert_type("0xffffffffffffffffffffffffff", DATA_TYPE_FLOAT);

	/* A malformed literal is refused, as the trailing "%c" used to do */
	check_convert_type("0x", DATA_TYPE_STRING);
	check_convert_type("0x10z", DATA_TYPE_STRING);
	check_convert_type("0x-10", DATA_TYPE_STRING);
	check_convert_type("0x 10", DATA_TYPE_STRING);

	/*
	 * Neither a sign nor leading whitespace is part of the "0x" prefix,
	 * so none of these reach the hex branch at all. The signed ones are
	 * picked up by the float converter as hex floats; the whitespace one
	 * is refused outright, unlike the decimal case above that strtoll()
	 * accepts.
	 */
	check_convert_type("-0x10", DATA_TYPE_FLOAT);
	check_convert_type("+0x10", DATA_TYPE_FLOAT);
	check_convert_type(" 0x10", DATA_TYPE_STRING);
	check_convert_forced_not_int("-0x10");
	check_convert_forced_not_int(" 0x10");

	/*
	 * Pin hex float notation directly. It is what the too wide literals
	 * above land on, so if "%lf" ever stopped accepting it those cases
	 * would start failing for a reason that has nothing to do with them.
	 */
	check_convert_type("0x1.8", DATA_TYPE_FLOAT);
	check_convert_forced_not_int("0x1.8");

	/* The hex branch runs before the digit scan, so force agrees */
	check_convert_forced_int("0x10", 16);
	check_convert_forced_int("0xFFFFFFFFFFFFFFFF", -1);
	check_convert_forced_not_int("0x10000000000000000");
	check_convert_forced_not_int("0x");

	/* A sign alone or in any other position is not an integer */
	check_convert_not_int("-");
	check_convert_not_int("--1");
	check_convert_not_int("1-2");
	check_convert_not_int("1-");
	check_convert_not_int("-1a");
	check_convert_not_int("- 1");
	check_convert_not_int("+");
	check_convert_not_int("++1");
	check_convert_not_int("1+2");
	check_convert_not_int("1+");
	check_convert_not_int("+1a");

	/* An explicit '+' is accepted, the same as an explicit '-' */
	check_convert_int("+1", 1);
	check_convert_int("+100", 100);
	check_convert_int("+0", 0);
	check_convert_int("+1234567890123456789", 1234567890123456789LL);
	check_convert_int("+9223372036854775807", 9223372036854775807LL);

	/* Trailing non-digits are not integers */
	check_convert_not_int("1a");

	/* Signed non-integers must still reach the float converter */
	check_convert_type("-1.5", DATA_TYPE_FLOAT);
	check_convert_type("-0.0", DATA_TYPE_FLOAT);
	check_convert_type("1.5", DATA_TYPE_FLOAT);
	check_convert_type("0.0", DATA_TYPE_FLOAT);
	check_convert_type("+1.5", DATA_TYPE_FLOAT);
}

END_TEST

START_TEST(test_check_match_float)
{
	/* Identical values must match */
	check_float_match(0.0, 0.0, true);
	check_float_match(1.5, 1.5, true);
	check_float_match(-1.5, -1.5, true);
	check_float_match(DBL_MAX, DBL_MAX, true);
	check_float_match(-DBL_MAX, -DBL_MAX, true);

	/* Zero has two representations that must compare as one value */
	check_float_match(0.0, -0.0, true);

	/* Different values must not match. */
	check_float_match(0.0, 1.0, false);
	check_float_match(1.0, 2.0, false);
	check_float_match(-1.0, 1.0, false);
	check_float_match(1.5, -1.5, false);
	check_float_match(DBL_MAX, -DBL_MAX, false);
	check_float_match(1e300, 2e300, false);

	/*
	 * Values within FUZZY_EPSILON match, since a float that went through
	 * a text round trip is not expected to come back bit identical. The
	 * tolerance is absolute and exclusive of the epsilon itself.
	 */
	check_float_match(1.0, 1.0 + (FUZZY_EPSILON / 2), true);
	check_float_match(1.0, 1.0 - (FUZZY_EPSILON / 2), true);
	check_float_match(0.0, (FUZZY_EPSILON / 2), true);
	check_float_match(0.0, FUZZY_EPSILON, false);
	check_float_match(0.0, -FUZZY_EPSILON, false);
	check_float_match(1.0, 1.0 + (FUZZY_EPSILON * 10), false);

	/*
	 * The tolerance is not transitive: eps/2 matches both 0.0 and eps,
	 * even though 0.0 and eps do not match each other (asserted above).
	 */
	check_float_match((FUZZY_EPSILON / 2), FUZZY_EPSILON, true);

	/*
	 * Being absolute, the tolerance also makes every value smaller than
	 * it indistinguishable from zero.
	 */
	check_float_match(0.0, DBL_MIN, true);

	/* NaN matches only NaN, whatever sign it carries */
	check_float_match(NAN, NAN, true);
	check_float_match(NAN, -NAN, true);
	check_float_match(NAN, 0.0, false);
	check_float_match(NAN, 1.0, false);
	check_float_match(NAN, DBL_MAX, false);
	check_float_match(NAN, INFINITY, false);
	check_float_match(NAN, -INFINITY, false);

	/* An infinity matches only an infinity of the same sign */
	check_float_match(INFINITY, INFINITY, true);
	check_float_match(-INFINITY, -INFINITY, true);
	check_float_match(INFINITY, -INFINITY, false);
	check_float_match(INFINITY, 0.0, false);
	check_float_match(-INFINITY, 0.0, false);
	check_float_match(INFINITY, DBL_MAX, false);
	check_float_match(-INFINITY, -DBL_MAX, false);
}

END_TEST

Suite *suite_data(void)
{
	Suite *s = suite_create("Data");
	TCase *tc_core = tcase_create("Data");

	tcase_add_test(tc_core, test_detection);
	tcase_add_test(tc_core, test_dict_typeset);
	tcase_add_test(tc_core, test_dict_iteration);
	tcase_add_test(tc_core, test_list_iteration);
	tcase_add_test(tc_core, test_convert_list_dict);
	tcase_add_test(tc_core, test_data_move);
	tcase_add_test(tc_core, test_convert_int);
	tcase_add_test(tc_core, test_check_match_float);

	suite_add_tcase(s, tc_core);
	return s;
}

#ifndef NDEBUG
static Suite *_suite_data_assert(void)
{
	Suite *s = suite_create("Data_assert");
	TCase *tc_core = tcase_create("Data_assert");

	tcase_add_test_raise_signal(tc_core, test_data_move_null_src, SIGABRT);
	tcase_add_test_raise_signal(tc_core, test_data_move_self, SIGABRT);

	suite_add_tcase(s, tc_core);
	return s;
}
#endif /* !NDEBUG */

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

	log_init("data-test", log_opts, 0, NULL);

	SRunner *sr = srunner_create(suite_data());

#ifdef NDEBUG
	printf("Can't perform assert tests with NDEBUG set.\n");
#else
	/* assert tests can only be run in forking mode */
	if (srunner_fork_status(sr) == CK_FORK)
		srunner_add_suite(sr, _suite_data_assert());
	else
		printf("Skipping assert tests since not in forking mode.\n");
#endif

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	log_fini();
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
