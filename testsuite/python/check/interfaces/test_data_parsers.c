/*****************************************************************************\
 *  Test for data_parser plugin
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
\*****************************************************************************/

#define _GNU_SOURCE
#include <check.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/plugrack.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"

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

static const struct {
	void (*test_func)(data_parser_t *parser, data_t *parent_path);
} tests_parse_dump[] = {

#define T(fn)                    \
	{                        \
		.test_func = fn, \
	}
	T(_test_DATA_PARSER_STRING),
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

extern int main(int argc, char **argv)
{
	int failures;

	/* Create main test case with fixtures and the main suite*/
	TCase *tcase = tcase_create("data_parser");
	tcase_add_unchecked_fixture(tcase, setup, teardown);
	tcase_add_test(tcase, test_list);
	tcase_add_test(tcase, test_load_current_plugin);
	tcase_add_test(tcase, test_load_current_plugin_complex);
	tcase_add_test(tcase, test_load_plugin_array);

	Suite *suite = suite_create("data_parser");
	suite_add_tcase(suite, tcase);

	/* Create and run the runner */
	SRunner *sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
