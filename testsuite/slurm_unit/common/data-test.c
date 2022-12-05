/*****************************************************************************\
 *  Copyright (C) 2019 SchedMD LLC
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <check.h>

#include "slurm/slurm_errno.h"
#include "src/common/data.h"
#include "src/common/log.h"
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

static data_for_each_cmd_t
	_find_dict_bool(const char *key, const data_t *data, void *arg)
{
	int *found = arg;

	ck_assert_msg(data_get_type(data) == DATA_TYPE_BOOL, "entry bool type");

	if (data_get_bool(data))
		(*found)++;

	ck_assert_ptr_ne(key, NULL);
	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t
	_invert_dict_bool(const char *key, data_t *data, void *arg)
{
	ck_assert_msg(data_get_type(data) == DATA_TYPE_BOOL, "entry bool type");
	ck_assert_ptr_ne(key, NULL);
	data_set_bool(data, !data_get_bool(data));
	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t
	_del_dict_bool_true(const char *key, data_t *data, void *arg)
{
	int *max = arg;

	ck_assert_ptr_ne(key, NULL);
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

	data_set_null(d);
	ck_assert_msg(data_get_type(d) == DATA_TYPE_NULL, "default type");

	FREE_NULL_DATA(d);
	ck_assert_msg(d == NULL, "free check");
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

Suite *suite_data(void)
{
	Suite *s = suite_create("Data");
	TCase *tc_core = tcase_create("Data");

	tcase_add_test(tc_core, test_detection);
	tcase_add_test(tc_core, test_dict_typeset);
	tcase_add_test(tc_core, test_dict_iteration);
	tcase_add_test(tc_core, test_list_iteration);

	suite_add_tcase(s, tc_core);
	return s;
}

int main(void)
{
	int number_failed;

	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("data-test", log_opts, 0, NULL);

	if (data_init()) {
		error("data_init() failed");
		return EXIT_FAILURE;
	}

	SRunner *sr = srunner_create(suite_data());

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	data_fini();
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
