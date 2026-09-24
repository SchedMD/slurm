/*****************************************************************************\
 *  Copyright (C) SchedMD LLC.
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *****************************************************************************
 *  Test for data_parser plugin
\*****************************************************************************/

#include <check.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/data.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/openapi.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"
#include "src/interfaces/serializer.h"

#define DATA_PARSER_PLUGINS_PER_RELEASE 4

static struct {
	int count;
	int current_plugin;
} test_foreach_list = {
	.count = 0,
	.current_plugin = 0,
};

static void setup(void)
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");

	/* Setup logging */
	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);
	log_init("data_parser-test", log_opts, 0, NULL);

	ck_assert(!slurm_conf_init(NULL));
}

static void teardown(void)
{
	slurm_conf_destroy();
	log_fini();
}

/*
 * Functions to be passed in functions to test like data_parser_g_new() or
 * data_parser_g_new_array(), so we can run ck_assert(false) in the callbacks
 * to handle errors and warning. That is, as data_parser_on_error_t or
 * data_parser_on_warn_t,
 */
__attribute__((format(printf, 5, 6))) static bool _fail_on_error(
	void *arg, data_parser_type_t type, int error_code, const char *source,
	const char *why, ...)
{
	ck_assert(false);
}

__attribute__((format(printf, 4, 5))) static void _fail_on_warn(
	void *arg, data_parser_type_t type, const char *source, const char *why,
	...)
{
	ck_assert(false);
}

/*
 * Function to be passed as plugrack_foreach_t to be able to use libcheck
 * code in such callbacks.
 */
static void _plugin_foreach_list(const char *full_type, const char *fq_path,
				 const plugin_handle_t id, void *arg)
{
	test_foreach_list.count++;
	ck_assert(test_foreach_list.count > 0);
	ck_assert(full_type);

	if (!xstrcasecmp(full_type, SLURM_DATA_PARSER_VERSION))
		test_foreach_list.current_plugin++;
}

static void _test_DATA_PARSER_STRING(data_parser_t *parser, data_t *parent_path)
{
	const char test_str[] = "SOME TEST STRING";
	const ssize_t test_str_bytes = sizeof(test_str);
	data_t *src = data_set_string(data_new(), test_str);
	char *dst = NULL;

	ck_assert(data_parser_g_resolve_openapi_type(parser, DATA_PARSER_STRING,
						     "INVALID") ==
		  OPENAPI_TYPE_INVALID);
	ck_assert(!xstrcasecmp(XSTRINGIFY(DATA_PARSER_STRING),
		data_parser_g_resolve_type_string(parser, DATA_PARSER_STRING)));

	ck_assert(!DATA_PARSE(parser, STRING, dst, src, parent_path));
	ck_assert(dst);
	ck_assert(xsize(dst) == test_str_bytes);
	ck_assert(!xstrcmp(test_str, dst));

	FREE_NULL_DATA(src);
	src = data_new();

	ck_assert(!DATA_DUMP(parser, STRING, dst, src));
	xfree(dst);

	ck_assert(src);
	ck_assert(data_get_type(src) == DATA_TYPE_STRING);
	ck_assert(!xstrcmp(data_get_string(src), test_str));

	FREE_NULL_DATA(src);
}

static void _test_DATA_PARSER_PRIORITY_BY_PARTITION(data_parser_t *parser,
						    data_t *parent_path)
{
	uint32_t priorities[] = { 100, 50 };
	slurm_job_info_t job = { 0 };
	data_t *dst, *entry;
	const char *version = data_parser_get_plugin_version(parser);
	int major, minor, micro;

	/* PRIORITY_BY_PARTITION is only supported in v0.0.42+ */
	if (sscanf(version, "v%d.%d.%d", &major, &minor, &micro) != 3)
		ck_abort_msg("Unexpected data_parser version format: %s",
			     version);
	if (SLURM_VERSION_NUM(major, minor, micro) <
	    SLURM_VERSION_NUM(0, 0, 42))
		return;

	dst = data_new();
	ck_assert(!DATA_DUMP(parser, PRIORITY_BY_PARTITION, job, dst));
	ck_assert(data_get_type(dst) == DATA_TYPE_LIST);
	ck_assert(data_get_list_length(dst) == 0);

	job.priority_array = priorities;
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
	/* Older releases require names for every nonempty priority array. */
	data_set_null(dst);
	ck_assert(!DATA_DUMP(parser, PRIORITY_BY_PARTITION, job, dst));
	ck_assert(data_get_type(dst) == DATA_TYPE_LIST);
	ck_assert(data_get_list_length(dst) == 0);
#endif

	data_set_null(dst);
	job.priority_array_names = xstrdup("first,second");
	ck_assert(!DATA_DUMP(parser, PRIORITY_BY_PARTITION, job, dst));
	ck_assert(data_get_list_length(dst) == 2);
	entry = data_list_dequeue(dst);
	ck_assert_str_eq(data_get_string(data_key_get(entry, "partition")),
			 "first");
	ck_assert_int_eq(data_get_int(data_key_get(entry, "priority")), 100);
	FREE_NULL_DATA(entry);
	entry = data_list_dequeue(dst);
	ck_assert_str_eq(data_get_string(data_key_get(entry, "partition")),
			 "second");
	ck_assert_int_eq(data_get_int(data_key_get(entry, "priority")), 50);
	FREE_NULL_DATA(entry);

	xfree(job.priority_array_names);
	FREE_NULL_DATA(dst);
}

static const struct {
	void (*test_func)(data_parser_t *parser, data_t *parent_path);
} tests_parse_dump[] = {

#define T(fn) \
	{ \
		.test_func = fn, \
	}
	T(_test_DATA_PARSER_STRING),
	T(_test_DATA_PARSER_PRIORITY_BY_PARTITION),
#undef T
};

static void _test_parse_dump(data_parser_t *parser)
{
	data_t *parent_path = data_set_list(data_new());
	data_set_string(data_list_append(parent_path), __func__);

	for (int i = 0; i < ARRAY_SIZE(tests_parse_dump); i++)
		tests_parse_dump[i].test_func(parser, parent_path);

	FREE_NULL_DATA(parent_path);
}

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
typedef struct {
	int errors;
	int last_error;
	int warnings;
} counts_t;

__attribute__((format(printf, 5, 6))) static bool _count_on_error(
	void *arg, data_parser_type_t type, int error_code, const char *source,
	const char *why, ...)
{
	counts_t *counts = arg;

	counts->errors++;
	counts->last_error = error_code;

	return false;
}

__attribute__((format(printf, 4, 5))) static void _count_on_warn(
	void *arg, data_parser_type_t type, const char *source, const char *why,
	...)
{
	counts_t *counts = arg;

	counts->warnings++;
}

/* Create dictionary with arbitrary keys and a value of every type */
static data_t *_new_arbitrary_dict(void)
{
	data_t *dict = data_set_dict(data_new());
	data_t *list = data_set_list(data_key_set(dict, "list"));
	data_t *nested = data_set_dict(data_key_set(dict, "dict"));

	data_set_string(data_key_set(dict, "string"), "value");
	data_set_int(data_key_set(dict, "int"), 100);
	data_set_float(data_key_set(dict, "float"), 1.5);
	data_set_bool(data_key_set(dict, "bool"), true);
	data_set_null(data_key_set(dict, "null"));
	data_set_string(data_list_append(list), "item");
	data_set_int(data_key_set(nested, "nested"), 1);

	return dict;
}

/* Verify dict matches _new_arbitrary_dict() without any type conversions */
static void _check_arbitrary_dict(data_t *dict)
{
	data_t *list = NULL, *nested = NULL, *value = NULL;

	ck_assert(data_get_type(dict) == DATA_TYPE_DICT);
	ck_assert(data_get_dict_length(dict) == 7);

	value = data_key_get(dict, "string");
	ck_assert(data_get_type(value) == DATA_TYPE_STRING);
	ck_assert(!xstrcmp(data_get_string(value), "value"));

	value = data_key_get(dict, "int");
	ck_assert(data_get_type(value) == DATA_TYPE_INT_64);
	ck_assert(data_get_int(value) == 100);

	value = data_key_get(dict, "float");
	ck_assert(data_get_type(value) == DATA_TYPE_FLOAT);
	ck_assert(data_get_float(value) == 1.5);

	value = data_key_get(dict, "bool");
	ck_assert(data_get_type(value) == DATA_TYPE_BOOL);
	ck_assert(data_get_bool(value));

	value = data_key_get(dict, "null");
	ck_assert(value);
	ck_assert(data_get_type(value) == DATA_TYPE_NULL);

	list = data_key_get(dict, "list");
	ck_assert(data_get_type(list) == DATA_TYPE_LIST);
	ck_assert(data_get_list_length(list) == 1);
	value = data_get_list_last(list);
	ck_assert(data_get_type(value) == DATA_TYPE_STRING);
	ck_assert(!xstrcmp(data_get_string(value), "item"));

	nested = data_key_get(dict, "dict");
	ck_assert(data_get_type(nested) == DATA_TYPE_DICT);
	ck_assert(data_get_dict_length(nested) == 1);
	value = data_key_get(nested, "nested");
	ck_assert(data_get_type(value) == DATA_TYPE_INT_64);
	ck_assert(data_get_int(value) == 1);
}

static void _test_DATA_PARSER_DATA_DICT(data_parser_t *parser,
					data_t *parent_path)
{
	data_t *src = _new_arbitrary_dict();
	data_t *dumped = data_new();
	data_t *dst = NULL;

	ck_assert(data_parser_g_resolve_openapi_type(parser,
						     DATA_PARSER_DATA_DICT,
						     NULL) ==
		  OPENAPI_TYPE_OBJECT);
	ck_assert(!xstrcasecmp(XSTRINGIFY(DATA_PARSER_DATA_DICT),
		data_parser_g_resolve_type_string(parser,
						  DATA_PARSER_DATA_DICT)));

	ck_assert(!DATA_PARSE(parser, DATA_DICT, dst, src, parent_path));
	ck_assert(dst);
	/* Parsing must copy the source instead of taking ownership */
	ck_assert(dst != src);
	_check_arbitrary_dict(src);
	_check_arbitrary_dict(dst);

	ck_assert(!DATA_DUMP(parser, DATA_DICT, dst, dumped));
	_check_arbitrary_dict(dumped);
	_check_arbitrary_dict(dst);

	/* Parsing again must replace the prior dictionary */
	FREE_NULL_DATA(src);
	src = data_set_dict(data_new());
	data_set_string(data_key_set(src, "replaced"), "value");
	ck_assert(!DATA_PARSE(parser, DATA_DICT, dst, src, parent_path));
	ck_assert(dst);
	ck_assert(data_get_dict_length(dst) == 1);
	ck_assert(data_key_get(dst, "replaced"));

	/* Parsing null must release the dictionary */
	data_set_null(src);
	ck_assert(!DATA_PARSE(parser, DATA_DICT, dst, src, parent_path));
	ck_assert(!dst);

	/* NULL dumps as an empty dictionary except in complex mode */
	FREE_NULL_DATA(dumped);
	dumped = data_new();
	ck_assert(!DATA_DUMP(parser, DATA_DICT, dst, dumped));
	if (data_parser_g_is_complex(parser)) {
		ck_assert(data_get_type(dumped) == DATA_TYPE_NULL);
	} else {
		ck_assert(data_get_type(dumped) == DATA_TYPE_DICT);
		ck_assert(!data_get_dict_length(dumped));
	}

	FREE_NULL_DATA(dumped);
	FREE_NULL_DATA(src);
}
#endif /* SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0) */

/* Test parsers that are only in the current data_parser plugin */
static void _test_current_parse_dump(data_parser_t *parser)
{
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
	data_t *parent_path = data_set_list(data_new());
	data_set_string(data_list_append(parent_path), __func__);

	_test_DATA_PARSER_DATA_DICT(parser, parent_path);

	FREE_NULL_DATA(parent_path);
#endif
}

START_TEST(test_list)
{
	ck_assert(!data_parser_g_new(_fail_on_error, _fail_on_error,
				     _fail_on_error, NULL, _fail_on_warn,
				     _fail_on_warn, _fail_on_warn, NULL, "list",
				     _plugin_foreach_list, true));
	ck_assert(test_foreach_list.count == DATA_PARSER_PLUGINS_PER_RELEASE);
	/* Current plugin should only list once */
	ck_assert(test_foreach_list.current_plugin == 1);
}

END_TEST

START_TEST(test_load_current_plugin)
{
	data_parser_t *parser = NULL;
	list_t *tres_list = list_create(slurmdb_destroy_tres_rec);
	list_t *qos_list = list_create(slurmdb_destroy_qos_rec);
	data_t *flags = data_new(), *flag = NULL;

	ck_assert(!data_parser_g_new(_fail_on_error, _fail_on_error,
				     _fail_on_error, NULL, _fail_on_warn,
				     _fail_on_warn, _fail_on_warn, NULL,
				     SLURM_DATA_PARSER_VERSION, NULL, true));

	parser = data_parser_g_new(_fail_on_error, _fail_on_error,
				   _fail_on_error, NULL, _fail_on_warn,
				   _fail_on_warn, _fail_on_warn, NULL,
				   SLURM_DATA_PARSER_VERSION, NULL, false);
	ck_assert(parser);

	/* can safely assign empty lists */
	ck_assert(!data_parser_g_assign(parser, DATA_PARSER_ATTR_TRES_LIST,
					tres_list));
	tres_list = NULL;
	ck_assert(!data_parser_g_assign(parser, DATA_PARSER_ATTR_QOS_LIST,
					qos_list));
	qos_list = NULL;

	ck_assert(!xstrcasecmp(data_parser_get_plugin(parser),
			       SLURM_DATA_PARSER_VERSION));
	ck_assert(xstrstr(SLURM_DATA_PARSER_VERSION,
			  data_parser_get_plugin_version(parser)));
	ck_assert(!data_parser_get_plugin_params(parser));

	ck_assert(!data_parser_g_dump_flags(parser, flags));
	ck_assert(data_get_type(flags) == DATA_TYPE_LIST);
	ck_assert(data_get_list_length(flags) == 1);
	flag = data_list_dequeue(flags);
	ck_assert(flag);
	ck_assert(data_get_type(flag) == DATA_TYPE_STRING);
	ck_assert(!xstrcasecmp(data_get_string(flag), "NONE"));

	ck_assert(!data_parser_g_is_complex(parser));
	ck_assert(!data_parser_g_is_deprecated(parser));

	_test_parse_dump(parser);
	_test_current_parse_dump(parser);

	FREE_NULL_DATA(flags);
	FREE_NULL_DATA(flag);
	FREE_NULL_DATA_PARSER(parser);
	FREE_NULL_LIST(tres_list);
	FREE_NULL_LIST(qos_list);
}

END_TEST

START_TEST(test_load_current_plugin_complex)
{
	data_parser_t *parser = NULL;
	data_t *flags = data_new(), *flag = NULL;

	parser = data_parser_g_new(_fail_on_error, _fail_on_error,
				   _fail_on_error, NULL, _fail_on_warn,
				   _fail_on_warn, _fail_on_warn, NULL,
				   SLURM_DATA_PARSER_VERSION_COMPLEX, NULL,
				   false);
	ck_assert(parser);

	ck_assert(!xstrcasecmp(data_parser_get_plugin(parser),
			       SLURM_DATA_PARSER_VERSION_COMPLEX));
	ck_assert(xstrstr(SLURM_DATA_PARSER_VERSION,
			  data_parser_get_plugin_version(parser)));
	ck_assert(!xstrcasecmp(data_parser_get_plugin_params(parser),
			       "+complex"));

	ck_assert(!data_parser_g_dump_flags(parser, flags));
	ck_assert(data_get_type(flags) == DATA_TYPE_LIST);
	ck_assert(data_get_list_length(flags) == 1);
	flag = data_list_dequeue(flags);
	ck_assert(flag);
	ck_assert(data_get_type(flag) == DATA_TYPE_STRING);
	ck_assert(!xstrcasecmp(data_get_string(flag), "COMPLEX"));

	ck_assert(data_parser_g_is_complex(parser));
	ck_assert(!data_parser_g_is_deprecated(parser));

	_test_parse_dump(parser);
	_test_current_parse_dump(parser);

	FREE_NULL_DATA(flags);
	FREE_NULL_DATA(flag);
	FREE_NULL_DATA_PARSER(parser);
}

END_TEST

START_TEST(test_load_plugin_array)
{
	data_parser_t **parsers = NULL;
	int count = 0, deprecated = 0;

	parsers = data_parser_g_new_array(_fail_on_error, _fail_on_error,
					  _fail_on_error, NULL, _fail_on_warn,
					  _fail_on_warn, _fail_on_warn, NULL,
					  "", NULL, false);
	ck_assert(parsers);
	ck_assert(parsers[0]);

	for (; parsers[count]; count++) {
		data_parser_t *parser = parsers[count];
		data_t *flags = NULL, *flag = NULL;

		if (!parser)
			break;

		flags = data_new();

		ck_assert(data_parser_get_plugin(parser));
		ck_assert(data_parser_get_plugin_version(parser));
		ck_assert(!data_parser_get_plugin_params(parser));

		ck_assert(!data_parser_g_dump_flags(parser, flags));
		ck_assert(data_get_type(flags) == DATA_TYPE_LIST);
		ck_assert(data_get_list_length(flags) == 1);
		flag = data_list_dequeue(flags);
		ck_assert(flag);
		ck_assert(data_get_type(flag) == DATA_TYPE_STRING);
		ck_assert(!xstrcasecmp(data_get_string(flag), "NONE"));

		ck_assert(!data_parser_g_is_complex(parser));

		if (data_parser_g_is_deprecated(parser))
			deprecated++;

		FREE_NULL_DATA(flags);
		FREE_NULL_DATA(flag);

		_test_parse_dump(parser);
	}
	ck_assert(count == DATA_PARSER_PLUGINS_PER_RELEASE);
	ck_assert(deprecated == 1);

	FREE_NULL_DATA_PARSER_ARRAY(parsers, false);
}

END_TEST

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
START_TEST(test_data_dict_reject)
{
	counts_t counts = { 0 };
	data_parser_t *parser = NULL;
	data_t *parent_path = data_set_list(data_new());
	data_t *src = data_new();
	data_t *dumped = data_new();
	data_t *dst = NULL;

	data_set_string(data_list_append(parent_path), __func__);

	/* Warnings about the OpenAPI type mismatch print the value as JSON */
	serializer_required(MIME_TYPE_JSON);

	parser = data_parser_g_new(_count_on_error, _count_on_error,
				   _count_on_error, &counts, _count_on_warn,
				   _count_on_warn, _count_on_warn, &counts,
				   SLURM_DATA_PARSER_VERSION, NULL, false);
	ck_assert(parser);

	/* Only a dictionary (or null) is accepted */
	data_set_string(src, "not a dictionary");
	ck_assert(DATA_PARSE(parser, DATA_DICT, dst, src, parent_path) ==
		  ESLURM_DATA_EXPECTED_DICT);
	ck_assert(!dst);
	ck_assert(counts.errors == 1);
	ck_assert(counts.last_error == ESLURM_DATA_EXPECTED_DICT);
	/* Warning about value not matching OpenAPI type */
	ck_assert(counts.warnings == 1);

	data_set_string(data_list_append(data_set_list(src)), "item");
	ck_assert(DATA_PARSE(parser, DATA_DICT, dst, src, parent_path) ==
		  ESLURM_DATA_EXPECTED_DICT);
	ck_assert(!dst);
	ck_assert(counts.errors == 2);
	ck_assert(counts.warnings == 2);

	/* Dumping anything but a dictionary is rejected */
	dst = data_set_list(data_new());
	ck_assert(DATA_DUMP(parser, DATA_DICT, dst, dumped) ==
		  ESLURM_DATA_EXPECTED_DICT);
	ck_assert(counts.errors == 3);
	ck_assert(counts.last_error == ESLURM_DATA_EXPECTED_DICT);
	/* Dumped value must still match the OpenAPI type */
	ck_assert(data_get_type(dumped) == DATA_TYPE_DICT);
	ck_assert(!data_get_dict_length(dumped));

	FREE_NULL_DATA(dst);
	FREE_NULL_DATA(dumped);
	FREE_NULL_DATA(src);
	FREE_NULL_DATA(parent_path);
	FREE_NULL_DATA_PARSER(parser);
}

END_TEST

START_TEST(test_data_dict_openapi_schema)
{
	const char plugin[] =
		SLURM_DATA_PARSER_VERSION SLURM_DATA_PARSER_PLUGIN_PARAMS_CHAR
		"spec_only";
	data_parser_t *parser = NULL;
	data_t *schema = data_new();
	data_t *dst = NULL, *type, *additional;

	parser = data_parser_g_new(_fail_on_error, _fail_on_error,
				   _fail_on_error, NULL, _fail_on_warn,
				   _fail_on_warn, _fail_on_warn, NULL, plugin,
				   NULL, false);
	ck_assert(parser);

	ck_assert(!DATA_DUMP(parser, DATA_DICT, dst, schema));
	ck_assert(data_get_type(schema) == DATA_TYPE_DICT);

	type = data_key_get(schema, "type");
	ck_assert(data_get_type(type) == DATA_TYPE_STRING);
	ck_assert(!xstrcmp(data_get_string(type), "object"));

	/* No properties are defined as any property name is allowed */
	ck_assert(!data_key_get(schema, "properties"));
	ck_assert(!data_key_get(schema, "format"));

	/* Empty schema allows property values of any type */
	additional = data_key_get(schema, "additionalProperties");
	ck_assert(data_get_type(additional) == DATA_TYPE_DICT);
	ck_assert(!data_get_dict_length(additional));

	FREE_NULL_DATA(schema);
	FREE_NULL_DATA_PARSER(parser);
}

END_TEST
#endif /* SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0) */

extern int main(int argc, char **argv)
{
	int failures;

	/* Create main test case with fixtures and the main suite*/
	TCase *tcase = tcase_create("data_parser");
	tcase_add_unchecked_fixture(tcase, setup, teardown);
	tcase_add_test(tcase, test_list);
	tcase_add_test(tcase, test_load_current_plugin);
	tcase_add_test(tcase, test_load_current_plugin_complex);
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 5, 0)
	/* Older releases include v0.0.41, which lacks PRIORITY_BY_PARTITION. */
	tcase_add_test(tcase, test_load_plugin_array);
#endif
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(26, 11, 0)
	tcase_add_test(tcase, test_data_dict_reject);
	tcase_add_test(tcase, test_data_dict_openapi_schema);
#endif

	Suite *suite = suite_create("data_parser");
	suite_add_tcase(suite, tcase);

	/* Create and run the runner */
	SRunner *sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
