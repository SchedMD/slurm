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

/*
 * This is a port of the old slurm_unit serializer-test.c. It runs under the
 * test_100_1 pytest meta-runner (compiled and run by atf.run_check_test() as a
 * single translation unit, so the large JSON data blobs are #include'd rather
 * than linked).
 *
 * It tests every plugin that serves application/json for round-trip
 * correctness. The suite runs once per implementation: serializer/json, when
 * it was built, and serializer/xjson. Cases that need constructs only JSON6
 * provides gate themselves on _active_grammar().
 */

/* _GNU_SOURCE required for HAVE_MALLINFO2 for better logging */
#define _GNU_SOURCE
#include "config.h"

#include <limits.h>

#if defined(__GLIBC__) && !defined(__UCLIBC__) && !defined(__MUSL__)
#include <features.h>
#if defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 33) && defined(__SIZEOF_INT128__)
#define HAVE_MALLINFO2
#include <malloc.h>
#endif
#endif
#endif

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xutf.h"
#include "src/interfaces/serializer.h"

#include "public_datasets/twitter.json.c"
#include "public_datasets/noaa_global_temps.json.c"

#define BYTES_IN_MiB (1024 * 1024)

#ifdef HAVE_MALLINFO2

typedef unsigned __int128 uint128_t;

typedef struct {
	uint128_t arena; /* non-mmapped space allocated from system */
	uint128_t ordblks; /* number of free chunks */
	uint128_t smblks; /* number of fastbin blocks */
	uint128_t hblks; /* number of mmapped regions */
	uint128_t hblkhd; /* space in mmapped regions */
	uint128_t usmblks; /* always 0, preserved for backwards compatibility */
	uint128_t fsmblks; /* space available in freed fastbin blocks */
	uint128_t uordblks; /* total allocated space */
	uint128_t fordblks; /* total free space */
	uint128_t keepcost; /* top-most, releasable (via malloc_trim) space */
} mallinfo2_128_t;

typedef struct {
	struct mallinfo2 peak;
	mallinfo2_128_t total;
	int count;
} mem_track_t;

#endif /* HAVE_MALLINFO2 */

static const char *mime_types[] = {
	MIME_TYPE_YAML,
	MIME_TYPE_JSON,
};

static const serializer_flags_t flag_combinations[] = {
	SER_FLAGS_COMPACT,
	SER_FLAGS_PRETTY,
	(SER_FLAGS_COMPACT | SER_FLAGS_COMPLEX),
	(SER_FLAGS_PRETTY | SER_FLAGS_COMPLEX),
};

/*
 * Run both layouts over the large datasets: the pretty printer is the path most
 * likely to have indentation or newline defects and these are the only
 * multi-megabyte documents that exercise it. SER_FLAGS_COMPLEX is deliberately
 * absent -- these datasets hold no infinities, so it would only duplicate a run
 * for no added signal.
 */
static const serializer_flags_t flag_combinations_large[] = {
	SER_FLAGS_COMPACT,
	SER_FLAGS_PRETTY,
};

static const struct {
	const char *source;
	const char *tag;
	const int run_count; /* diff count to avoid test running too long */
} test_json[] = { { test_json1, "twitter-dataset", 25 },
		  { test_json2, "NOAA-ocean-temps", 50 } };

/*
 * Grammar served by the plugin bound to MIME_TYPE_JSON.
 *
 * serializer/json implements RFC 8259 only. serializer/xjson implements JSON6,
 * a superset, so it accepts everything RFC 8259 does plus comments, single
 * quoted and unquoted strings, line continuations and a wider escape set.
 * Documents are classified by the grammar they require rather than by plugin
 * name so that a second JSON6 implementation would need no changes here.
 */
typedef enum {
	GRAMMAR_JSON = SLURM_BIT(0), /* RFC 8259 */
	GRAMMAR_JSON6 = SLURM_BIT(1), /* JSON6 superset of RFC 8259 */
} grammar_t;

/* Override which plugin serves MIME_TYPE_JSON: see _json_plugin() */
#define SERIALIZER_ENV "SLURM_TESTSUITE_SERIALIZER"

#define SERIALIZER_PREFIX "serializer/"

/* Strip the optional "serializer/" prefix that load_plugins() also accepts */
static const char *_bare_plugin(const char *plugin)
{
	if (!xstrncmp(plugin, SERIALIZER_PREFIX, strlen(SERIALIZER_PREFIX)))
		return (plugin + strlen(SERIALIZER_PREFIX));

	return plugin;
}

/*
 * Is plugin named in SerializerPlugins?
 *
 * load_plugins() takes a comma separated list and accepts each entry with or
 * without the "serializer/" prefix, so match the same way.
 */
static bool _conf_selects(const char *plugin)
{
	const char *bare = _bare_plugin(plugin);
	char *list = NULL, *type = NULL, *last = NULL;
	bool found = false;

	if (!slurm_conf.serializer_plugins)
		return false;

	list = xstrdup(slurm_conf.serializer_plugins);

	for (type = strtok_r(list, ",", &last); type;
	     type = strtok_r(NULL, ",", &last)) {
		if (!xstrcmp(_bare_plugin(type), bare)) {
			found = true;
			break;
		}
	}

	xfree(list);
	return found;
}

/*
 * Plugin that must serve MIME_TYPE_JSON for the tcase being run.
 *
 * Set by the tcase fixture before any test runs, so that the same tests run
 * once per implementation. Always one of the MIME_TYPE_*_PLUGIN constants, so
 * it is already in the "serializer/<name>" form load_plugins() normalizes to.
 */
static const char *json_plugin = NULL;

static const char *_json_plugin(void)
{
	ck_assert_msg(json_plugin, "tcase fixture must select a plugin");
	return json_plugin;
}

/* Grammar the configured JSON plugin is required to implement */
static grammar_t _active_grammar(void)
{
	bool json, xjson;

	/*
	 * An unset SerializerPlugins loads every plugin found in PluginDir and
	 * the binding for MIME_TYPE_JSON then depends on readdir() order, so
	 * the grammar under test would be undefined.
	 */
	ck_assert_msg(slurm_conf.serializer_plugins,
		      "SerializerPlugins must be set to bind " MIME_TYPE_JSON);

	json = _conf_selects(MIME_TYPE_JSON_PLUGIN);
	xjson = _conf_selects(MIME_TYPE_XJSON_PLUGIN);

	ck_assert_msg((json != xjson),
		      "exactly one of %s or %s must be configured, got \"%s\"",
		      MIME_TYPE_JSON_PLUGIN, MIME_TYPE_XJSON_PLUGIN,
		      slurm_conf.serializer_plugins);

	if (xjson)
		return (GRAMMAR_JSON | GRAMMAR_JSON6);

	return GRAMMAR_JSON;
}

static void _setup(void)
{
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);
	log_init("serializer-test", log_opts, 0, NULL);

	ck_assert(!slurm_conf_init(NULL));

	/*
	 * Pin which plugins load. Leaving SerializerPlugins unset loads every
	 * plugin in PluginDir, and serializer/json and serializer/xjson declare
	 * the same mime_types[], so only the first one registered would serve
	 * application/json. Naming the plugin makes the implementation under
	 * test explicit rather than discovered.
	 */
	xfree(slurm_conf.serializer_plugins);
	slurm_conf.serializer_plugins =
		xstrdup_printf("%s,%s", _json_plugin(), MIME_TYPE_YAML_PLUGIN);

	/*
	 * Pin the flags too. Every case below asserts against the plugin's
	 * default layout and substitution behavior, so an inherited
	 * SerializerParameters or SLURM_JSON/SLURM_YAML from the environment
	 * would change what the dumper emits and fail cases that are correct.
	 * json6 is the sharpest example: it turns the U+FFFD substitution that
	 * _test_utf8_case() expects into \xNN escapes.
	 */
	xfree(slurm_conf.serializer_params);
	unsetenv(ENV_CONFIG_JSON);
	unsetenv(ENV_CONFIG_YAML);

	serializer_required(MIME_TYPE_JSON);
	serializer_required(MIME_TYPE_YAML);
}

/*
 * One fixture per implementation. tcase fixtures take no arguments, so each
 * selects its plugin before the shared setup runs.
 */
static void _setup_json(void)
{
	json_plugin = MIME_TYPE_JSON_PLUGIN;
	_setup();
}

static void _setup_xjson(void)
{
	json_plugin = MIME_TYPE_XJSON_PLUGIN;
	_setup();
}

static void teardown(void)
{
	serializer_g_fini();
	slurm_conf_destroy();
	log_fini();
}

/* serialize src to a string, re-parse it, and verify the round-trip matches */
static void _test_run(const char *tag, data_t *src, const char *mime_type,
		      const serializer_flags_t flags)
{
	char *output = NULL;
	size_t output_len = -1;
	data_t *verify_src = NULL;
	int rc;

	rc = serialize_g_data_to_string(&output, &output_len, src, mime_type,
					flags);
	ck_assert_int_eq(rc, 0);

	debug("dumped %s with %s:\n%s\n\n\n\n", tag, mime_type, output);

	rc = serialize_g_string_to_data(&verify_src, output, output_len,
					mime_type);
	ck_assert_int_eq(rc, 0);

	ck_assert_msg(data_check_match(src, verify_src, false),
		      "match verification failed");

	xfree(output);
	FREE_NULL_DATA(verify_src);
}

START_TEST(test_mimetype)
{
	const char *expected = _json_plugin();
	const grammar_t grammar = _active_grammar();
	const char *ptr = NULL;

	/*
	 * The plugin named in SerializerPlugins must be the one that actually
	 * won the mime type. They differ if the plugin failed to load or lost
	 * the type to another plugin, which would silently test the wrong
	 * implementation.
	 */
	ck_assert(resolve_mime_type(MIME_TYPE_JSON, &ptr) != NULL);
	ck_assert(ptr != NULL);
	ck_assert_str_eq(ptr, expected);

	/* Grammar must follow the bound plugin */
	if (!xstrcmp(ptr, MIME_TYPE_XJSON_PLUGIN))
		ck_assert(grammar & GRAMMAR_JSON6);
	else
		ck_assert(!(grammar & GRAMMAR_JSON6));

	ck_assert(grammar & GRAMMAR_JSON);

	ptr = NULL;
	ck_assert(resolve_mime_type("application/jsonrequest", &ptr) != NULL);
	ck_assert(ptr != NULL);
	ck_assert_str_eq(ptr, expected);
}

END_TEST

START_TEST(test_parse_invalid)
{
	/* malformed JSON that the parser must reject */
	static const char
		*sf[] = {
			"\"taco",
			"taco\"",
			"[",
			"]",
			"{",
			"}",
			"[{",
			"{[",
			"{[}",
			"[{}",
			"[\"taco",
			"{\"taco",
			"{\"taco:",
			"{taco:",
			"{\"taco\":",
			"[taco:",
			"[\"taco\":",
			"[\"taco\",:",
			",,,,]",
			",:,,]",
			"\\,",
			":",
			",:,",
			"\"\\\"",
			"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[",
			"{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:{test:test}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}",
			"{\"taco\"::taco}",
			"{::taco}",
			"\xFE",
			"\xFF",
			"\xFE\xFF",
			"\xFF\xFE",
			"\xFE\xFF\x00\x00",
			"\xFEtaco",
			"\xFFtaco",
			"\xFE\xFFtaco",
			"\xFF\xFEtaco",
			"\xFE\xFF\x00\x00taco",
			"\x01",
			"\x02",
			"\x03",
			"\x04",
			"\x05",
			"\x06",
			"\x07",
			"\x08",
			"\\u0",
			"\\uTACOS",
			"\\u000TACOS",
			"tacos\"tacos\"taco\"\"",
			"*\"tacos\"taco\"\"",
			"*,0",
		};

	for (int i = 0; i < ARRAY_SIZE(sf); i++) {
		int rc;
		data_t *d = NULL;

		rc = serialize_g_string_to_data(&d, sf[i], strlen(sf[i]),
						MIME_TYPE_JSON);
		debug("expected fail source %d=%d -> %pD\n%s\n\n\n\n", i, rc, d,
		      sf[i]);
		ck_assert_ptr_null(d);

		FREE_NULL_DATA(d);
	}
}

END_TEST

START_TEST(test_parse_valid)
{
	/* valid JSON that must parse, match, and round-trip */
	static const char *s[] = {
		"\"taco\"",
		"\"\\\"taco\\\"\"",
		"[ 100 ]",
		"[ 100.389 ]",
		"[ -100.389 ]",
		"[ 1.1238e10 ]",
		"[ -1.1238e10 ]",
		"{ \"taco\": \"tacos\" }",
		"[ \"taco1\", \"taco2\", ]",
		"[ \"taco1\", \"taco2\", \"taco3\" ]",
		"[ true, false ]",
		"{\t\t\t\n}",
		"{ }",
		"[ \"\\u0024\", \"\\u00a3\", \"\\u00c0\", \"\\u0418\", \"\\u0939\", \"\\u20ac\", \"\\ud55c\" ]",
		"[]",
		"[]",
		"{}",
		"[]",
		"[[]   \t]",
		"[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]",
		"[{\"test\":\"test\"}]",
		"{\"test\":[]}",
		"{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":{\"test\":\"test\"}}}}}}}}}}}}}}}}}}}}}}}}}}",
		"\"\\u10FFFF\"",
		"\"\\u10FFFFFFFFFFFFFFFFFFFFFFF\"",
	};
	data_t *c[] = {
		data_set_string(data_new(), "taco"),
		data_set_string(data_new(), "\"taco\""),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_dict(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_dict(data_new()),
		data_set_dict(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_dict(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_list(data_new()),
		data_set_dict(data_new()),
		data_set_dict(data_new()),
		data_set_string(data_new(), "\u10FFFF"),
		data_set_string(data_new(), "\u10FFFFFFFFFFFFFFFFFFFFFFF"),
	};

	data_set_int(data_list_append(c[2]), 100);
	data_set_float(data_list_append(c[3]), 100.389);
	data_set_float(data_list_append(c[4]), -100.389);
	data_set_float(data_list_append(c[5]), 1.1238e10);
	data_set_float(data_list_append(c[6]), -1.1238e10);

	data_set_string(data_key_set(c[7], "taco"), "tacos");

	data_set_string(data_list_append(c[8]), "taco1");
	data_set_string(data_list_append(c[8]), "taco2");

	data_set_string(data_list_append(c[9]), "taco1");
	data_set_string(data_list_append(c[9]), "taco2");
	data_set_string(data_list_append(c[9]), "taco3");

	data_set_bool(data_list_append(c[10]), true);
	data_set_bool(data_list_append(c[10]), false);

	data_set_string(data_list_append(c[13]), "\U00000024");
	data_set_string(data_list_append(c[13]), "\U000000a3");
	data_set_string(data_list_append(c[13]), "\U000000c0");
	data_set_string(data_list_append(c[13]), "\U00000418");
	data_set_string(data_list_append(c[13]), "\U00000939");
	data_set_string(data_list_append(c[13]), "\U000020ac");
	data_set_string(data_list_append(c[13]), "\U0000d55c");

	data_set_list(data_list_append(c[18]));

	{
		data_t *t = c[19];
		for (int i = 0; i < 19; i++)
			t = data_set_list(data_list_append(t));
	}

	data_set_string(data_key_set(data_set_dict(data_list_append(c[20])),
				     "test"),
			"test");

	data_set_list(data_key_set(c[21], "test"));

	{
		data_t *t = c[22];
		for (int i = 0; i < 26; i++)
			t = data_set_dict(data_key_set(t, "test"));

		data_set_string(t, "test");
	}

	for (int i = 0; i < ARRAY_SIZE(s); i++) {
		int rc;
		data_t *d = NULL;

		rc = serialize_g_string_to_data(&d, s[i], strlen(s[i]),
						MIME_TYPE_JSON);
		debug("expected pass source %d=%d -> %pD\n%s\n\n\n\n", i, rc, d,
		      s[i]);
		ck_assert_int_eq(rc, 0);

		data_convert_tree(d, DATA_TYPE_NONE);
		ck_assert_msg(data_check_match(c[i], d, false),
			      "verify failed: %s", s[i]);

		for (int f = 0; f < ARRAY_SIZE(flag_combinations); f++) {
			for (int m = 0; m < ARRAY_SIZE(mime_types); m++) {
				const char *mptr = NULL;
				const char *mime_type =
					resolve_mime_type(mime_types[m], &mptr);

				if (mime_type)
					_test_run(s[i], d, mime_type,
						  flag_combinations[f]);
				else
					debug("skipping test with %s",
					      mime_types[m]);
			}
		}

		FREE_NULL_DATA(d);
	}

	for (int i = 0; i < ARRAY_SIZE(c); i++)
		FREE_NULL_DATA(c[i]);
}

END_TEST

START_TEST(test_compliance_large)
{
	/*
	 * Verify that the same JSON file can be parsed and dumped with same
	 * contents.
	 */
	for (int i = 0; i < ARRAY_SIZE(test_json); i++) {
		int rc;
		data_t *data = NULL;
		const int len = strlen(test_json[i].source);

		debug("source %s:\n%s\n\n\n\n", test_json[i].tag,
		      test_json[i].source);

		rc = serialize_g_string_to_data(&data, test_json[i].source, len,
						MIME_TYPE_JSON);
		ck_assert_int_eq(rc, 0);

		for (int f = 0; f < ARRAY_SIZE(flag_combinations_large); f++) {
			for (int m = 0; m < ARRAY_SIZE(mime_types); m++) {
				const char *mptr = NULL;
				const char *mime_type =
					resolve_mime_type(mime_types[m], &mptr);

				if (mime_type)
					_test_run(test_json[i].tag, data,
						  mime_type,
						  flag_combinations_large[f]);
				else
					debug("skipping test with %s",
					      mime_types[m]);
			}
		}

		FREE_NULL_DATA(data);
	}
}

END_TEST

#ifdef HAVE_MALLINFO2
static void _track_mem(mem_track_t *track)
{
	struct mallinfo2 mi = mallinfo2();

	track->peak.arena = MAX(mi.arena, track->peak.arena);
	track->peak.ordblks = MAX(mi.ordblks, track->peak.ordblks);
	track->peak.hblks = MAX(mi.hblks, track->peak.hblks);
	track->peak.hblkhd = MAX(mi.hblkhd, track->peak.hblkhd);
	track->peak.usmblks = MAX(mi.usmblks, track->peak.usmblks);
	track->peak.fsmblks = MAX(mi.fsmblks, track->peak.fsmblks);
	track->peak.uordblks = MAX(mi.uordblks, track->peak.uordblks);
	track->peak.fordblks = MAX(mi.fordblks, track->peak.fordblks);
	track->peak.keepcost = MAX(mi.keepcost, track->peak.keepcost);

	track->total.arena += mi.arena;
	track->total.ordblks += mi.ordblks;
	track->total.hblks += mi.hblks;
	track->total.hblkhd += mi.hblkhd;
	track->total.usmblks += mi.usmblks;
	track->total.fsmblks += mi.fsmblks;
	track->total.uordblks += mi.uordblks;
	track->total.fordblks += mi.fordblks;
	track->total.keepcost += mi.keepcost;

	track->count++;
}

static void _print_tracked_mem(mem_track_t *track, const char *type)
{
	struct mallinfo2 avg = {
		.arena = track->total.arena / track->count,
		.ordblks = track->total.ordblks / track->count,
		.hblks = track->total.hblks / track->count,
		.hblkhd = track->total.hblkhd / track->count,
		.usmblks = track->total.usmblks / track->count,
		.fsmblks = track->total.fsmblks / track->count,
		.uordblks = track->total.uordblks / track->count,
		.fordblks = track->total.fordblks / track->count,
		.keepcost = track->total.keepcost / track->count,
	};

	printf("\t%s Total non-mmapped bytes (arena):       %zu/%zuB %zu/%zuMiB\n",
	       type, avg.arena, track->peak.arena, (avg.arena / 1048576),
	       (track->peak.arena / 1048576));
	printf("\t%s # of free chunks (ordblks):            %zu/%zu\n", type,
	       avg.ordblks, track->peak.ordblks);
	printf("\t%s # of free fastbin blocks (smblks):     %zu/%zu\n", type,
	       avg.smblks, track->peak.smblks);
	printf("\t%s # of mapped regions (hblks):           %zu/%zu\n", type,
	       avg.hblks, track->peak.hblks);
	printf("\t%s Bytes in mapped regions (hblkhd):      %zu/%zu\n", type,
	       avg.hblkhd, track->peak.hblkhd);
	printf("\t%s Max. total allocated space (usmblks):  %zu/%zu\n", type,
	       avg.usmblks, track->peak.usmblks);
	printf("\t%s Free bytes held in fastbins (fsmblks): %zu/%zu\n", type,
	       avg.fsmblks, track->peak.fsmblks);
	printf("\t%s Total allocated space (uordblks):      %zu/%zu\n", type,
	       avg.uordblks, track->peak.uordblks);
	printf("\t%s Total free space (fordblks):           %zu/%zu\n", type,
	       avg.fordblks, track->peak.fordblks);
	printf("\t%s Topmost releasable block (keepcost):   %zu/%zu\n\n", type,
	       avg.keepcost, track->peak.keepcost);
}
#else /* !HAVE_MALLINFO2 */
/* avoid compile errors for undefined/unused variables */
typedef struct {
	int a;
	int b;
} mem_track_t;

#define _track_mem(track) ((void) (track))
#define _print_tracked_mem(track, type) ((void) (track))
#endif /* !HAVE_MALLINFO2 */

static void _test_bandwidth_str(const char *tag, const char *source,
				const int run_count)
{
	int rc;
	data_t *data = NULL;
	const int test_json_len = strlen(source);
	char *output = NULL;
	size_t output_len = 0;
	timespec_t read_times = { 0, 0 }, write_times = { 0, 0 };
	uint64_t total_written = 0, total_read = 0;
	timespec_t fastest_write = { UINT_MAX, 0 };
	timespec_t fastest_read = { UINT_MAX, 0 };
	double read_avg, write_avg;
	double read_diff, write_diff, read_rate, write_rate;
	double read_rate_bytes, write_rate_bytes;
	double fastest_read_rate_bytes, fastest_write_rate_bytes;
	double fastest_read_rate, fastest_write_rate;
	mem_track_t read_mem = { 0 }, write_mem = { 0 };

	for (int i = 0; i < run_count; i++) {
		DEF_TIMERS;
		timespec_t duration = { 0, 0 };

		FREE_NULL_DATA(data);

		_track_mem(&read_mem);

		START_TIMER;
		rc = serialize_g_string_to_data(&data, source, test_json_len,
						MIME_TYPE_JSON);
		END_TIMER3(__func__, INFINITE);
		duration = timespec_diff_ns(TIMER_END_TS, TIMER_START_TS).diff;

		_track_mem(&read_mem);

		total_read += test_json_len;
		read_times = timespec_add(read_times, duration);

		if (timespec_is_after(fastest_read, duration))
			fastest_read = duration;

		ck_assert_int_eq(rc, 0);
	}

	for (int i = 0; i < run_count; i++) {
		DEF_TIMERS;
		timespec_t duration = { 0, 0 };

		_track_mem(&write_mem);

		START_TIMER;
		rc = serialize_g_data_to_string(&output, &output_len, data,
						MIME_TYPE_JSON,
						SER_FLAGS_PRETTY);
		END_TIMER3(__func__, INFINITE);
		duration = timespec_diff_ns(TIMER_END_TS, TIMER_START_TS).diff;

		_track_mem(&write_mem);

		total_written += output_len;
		write_times = timespec_add(write_times, duration);

		if (timespec_is_after(fastest_write, duration))
			fastest_write = duration;

		xfree(output);
		output_len = 0;

		ck_assert_int_eq(rc, 0);
	}

	FREE_NULL_DATA(data);

	read_diff = timespec_to_secs(read_times) / run_count;
	write_diff = timespec_to_secs(write_times) / run_count;

	read_avg = total_read / run_count;
	write_avg = total_written / run_count;

	/* (bytes / sec) * (1 MiB / 1024*1024 bytes) */
	read_rate_bytes = (read_avg / read_diff);
	write_rate_bytes = (write_avg / write_diff);
	fastest_read_rate_bytes = read_avg / timespec_to_secs(fastest_read);
	fastest_write_rate_bytes = write_avg / timespec_to_secs(fastest_write);

	read_rate = read_rate_bytes / BYTES_IN_MiB;
	write_rate = write_rate_bytes / BYTES_IN_MiB;
	fastest_read_rate = fastest_read_rate_bytes / BYTES_IN_MiB;
	fastest_write_rate = fastest_write_rate_bytes / BYTES_IN_MiB;

	printf("%s: %u runs:\n", tag, run_count);

	printf("\tfastest read=%lf sec\n\tfastest write=%lf sec\n\n",
	       timespec_to_secs(fastest_read), timespec_to_secs(fastest_write));

	printf("\tfastest read=%f MiB/sec \n\tfastest write=%f MiB/sec\n\n",
	       fastest_read_rate, fastest_write_rate);

	printf("\tavg read=%lf sec\n\tavg write=%lf sec\n\n", read_diff,
	       write_diff);

	printf("\tavg read=%f MiB/sec \n\tavg write=%f MiB/sec\n\n", read_rate,
	       write_rate);

	_print_tracked_mem(&read_mem, "read");
	_print_tracked_mem(&write_mem, "write");
}

START_TEST(test_bandwidth)
{
	for (int i = 0; i < ARRAY_SIZE(test_json); i++)
		_test_bandwidth_str(test_json[i].tag, test_json[i].source,
				    test_json[i].run_count);
}

END_TEST

/*
 * Strings that can not be dumped as conformant JSON.
 *
 * Values are built from explicit byte arrays and never from C string literals:
 * a literal such as "\u10FFFF" is decoded by the C compiler with its own
 * escape rules, which are not JSON's, so what reaches the serializer would not
 * be what the test appears to say.
 *
 * SER_FLAGS_JSON6 dumps the source bytes as \xNN and must round trip
 * exactly. Without it every byte sequence below is replaced with U+FFFD, which
 * is lossy but leaves valid JSON.
 */
static const struct {
	const char *tag;
	/*
	 * Grammar whose dumper this row describes. The substitution and \xNN
	 * behaviour below is serializer/xjson's; serializer/json passes bytes
	 * that are not valid UTF-8 straight through instead, so those rows are
	 * skipped unless a JSON6 plugin is bound. Pinning serializer/json's
	 * passthrough here would cement it, and emitting invalid UTF-8 is
	 * itself a violation of RFC 8259 section 8.1.
	 */
	const grammar_t requires;
	const utf8_t in[8];
	const size_t in_bytes;
	/* expected value after dump+parse without SER_FLAGS_COMPLEX */
	const utf8_t expect[8];
	const size_t expect_bytes;
} utf8_cases[] = {
	{ "private-use-U+E000",
	  GRAMMAR_JSON6,
	  { 'a', 0xee, 0x80, 0x80, 'z' },
	  5,
	  { 'a', 0xef, 0xbf, 0xbd, 'z' },
	  5 },
	{ "private-use-U+F8FF",
	  GRAMMAR_JSON6,
	  { 'a', 0xef, 0xa3, 0xbf, 'z' },
	  5,
	  { 'a', 0xef, 0xbf, 0xbd, 'z' },
	  5 },
	{ "astral-PUA-U+F0000",
	  GRAMMAR_JSON6,
	  { 'a', 0xf3, 0xb0, 0x80, 0x80, 'z' },
	  6,
	  { 'a', 0xef, 0xbf, 0xbd, 'z' },
	  5 },
	{ "noncharacter-U+FFFE",
	  GRAMMAR_JSON6,
	  { 'a', 0xef, 0xbf, 0xbe, 'z' },
	  5,
	  { 'a', 0xef, 0xbf, 0xbd, 'z' },
	  5 },
	{ "cesu8-surrogate",
	  GRAMMAR_JSON6,
	  { 'a', 0xed, 0xa0, 0x80, 'z' },
	  5,
	  { 'a', 0xef, 0xbf, 0xbd, 'z' },
	  5 },
	{ "beyond-U+10FFFF",
	  GRAMMAR_JSON6,
	  { 'a', 0xf4, 0x90, 0x80, 0x80, 'z' },
	  6,
	  { 'a', 0xef, 0xbf, 0xbd, 'z' },
	  5 },
	{ "overlong",
	  GRAMMAR_JSON6,
	  { 'a', 0xc0, 0xb3, 'z' },
	  4,
	  { 'a', 0xef, 0xbf, 0xbd, 'z' },
	  5 },
	{ "bad-continuation",
	  GRAMMAR_JSON6,
	  { 'a', 0xe0, 0xa0, 'A', 'z' },
	  5,
	  { 'a', 0xef, 0xbf, 0xbd, 'A', 'z' },
	  6 },
	{ "lone-continuation",
	  GRAMMAR_JSON6,
	  { 'a', 0xff, 'z' },
	  3,
	  { 'a', 0xef, 0xbf, 0xbd, 'z' },
	  5 },
	/* valid UTF-8 must be untouched by every grammar and both modes */
	{ "ascii", GRAMMAR_JSON, { 'h', 'i' }, 2, { 'h', 'i' }, 2 },
	{ "emoji",
	  GRAMMAR_JSON,
	  { 'a', 0xf0, 0x9f, 0x8c, 0xae, 'z' },
	  6,
	  { 'a', 0xf0, 0x9f, 0x8c, 0xae, 'z' },
	  6 },
};

/*
 * Dump src, verify the JSON, parse it back and compare against want[].
 * Dumps the parsed value a second time to verify the output is idempotent: a
 * substitution that drifted would produce different JSON on the second pass.
 */
static void _test_utf8_case(const char *tag, const utf8_t *in, size_t in_bytes,
			    const utf8_t *want, size_t want_bytes,
			    serializer_flags_t flags)
{
	data_t *src = data_set_dict(data_new()), *back = NULL;
	char *out = NULL, *again = NULL;
	char *value = xstrndup((const char *) in, in_bytes);
	size_t out_bytes = 0, again_bytes = 0;
	const char *parsed = NULL;
	const bool json6 = (flags & SER_FLAGS_JSON6);

	data_set_string_own(data_key_set(src, "k"), value);

	ck_assert_int_eq(serialize_g_data_to_string(&out, &out_bytes, src,
						    MIME_TYPE_JSON, flags),
			 0);

	/*
	 * \u0000 is never a valid escape to emit: the parser rejects it and
	 * conformant parsers decode it to a NUL that truncates the value.
	 */
	ck_assert_msg(!xstrstr(out, "\\u0000"), "%s: dumped \\u0000 escape: %s",
		      tag, out);

	if (!json6)
		ck_assert_msg(
			!xstrstr(out, "\\x"),
			"%s: dumped \\x escape without SER_FLAGS_JSON6: %s",
			tag, out);

	ck_assert_int_eq(serialize_g_string_to_data(&back, out, out_bytes,
						    MIME_TYPE_JSON),
			 0);

	parsed = data_get_string(data_key_get(back, "k"));
	ck_assert_ptr_nonnull(parsed);

	if (json6) {
		/* source bytes are escaped verbatim and must survive exactly */
		ck_assert_msg((strlen(parsed) == in_bytes) &&
				      !memcmp(parsed, in, in_bytes),
			      "%s: SER_FLAGS_JSON6 did not round trip: %s", tag,
			      out);
	} else {
		ck_assert_msg((strlen(parsed) == want_bytes) &&
				      !memcmp(parsed, want, want_bytes),
			      "%s: unexpected substitution: %s", tag, out);
	}

	ck_assert_int_eq(serialize_g_data_to_string(&again, &again_bytes, back,
						    MIME_TYPE_JSON, flags),
			 0);
	ck_assert_msg(!xstrcmp(out, again),
		      "%s: dump is not idempotent: %s then %s", tag, out,
		      again);

	xfree(out);
	xfree(again);
	FREE_NULL_DATA(src);
	FREE_NULL_DATA(back);
}

/*
 * RFC 8259 documents exercising every legal comma position.
 *
 * JSON6 grammar extensions must never change how a conformant document parses.
 * Array elision is the one with teeth: it appends an element when a comma
 * arrives with no value to its left, which RFC 8259 section 5 makes impossible
 * for a conformant array, since every comma there sits between two values. If
 * an extension ever fires on conformant input the element count moves, so
 * pinning the exact lengths here is what catches it.
 *
 * nulls is tracked separately because a leaked elision shows up as an extra
 * DATA_TYPE_NULL element rather than as a wrong value.
 */
static const struct {
	const char *tag;
	const char *doc;
	const bool is_list;
	const size_t length; /* list or dict entries */
	const size_t nulls; /* DATA_TYPE_NULL elements, lists only */
} rfc_comma_cases[] = {
	{ "empty-list", "[]", true, 0, 0 },
	{ "one", "[1]", true, 1, 0 },
	{ "two", "[1,2]", true, 2, 0 },
	{ "three", "[1,2,3]", true, 3, 0 },
	{ "five", "[1,2,3,4,5]", true, 5, 0 },
	{ "spaced", "[ 1 , 2 , 3 ]", true, 3, 0 },
	{ "newlines", "[1,\n2,\n3]", true, 3, 0 },
	{ "strings", "[\"a\",\"b\",\"c\"]", true, 3, 0 },
	{ "mixed-types", "[0,-1,1.5,true,false,null]", true, 6, 1 },
	{ "explicit-nulls", "[null,null]", true, 2, 2 },
	{ "nested-lists", "[[1,2],[3,4]]", true, 2, 0 },
	{ "empty-nested", "[[],[]]", true, 2, 0 },
	{ "list-of-dicts", "[{\"a\":1},{\"b\":2}]", true, 2, 0 },
	{ "dict-two-keys", "{\"a\":1,\"b\":2}", false, 2, 0 },
	{ "dict-three-keys", "{\"a\":1,\"b\":2,\"c\":3}", false, 3, 0 },
	{ "dict-of-lists", "{\"a\":[1,2],\"b\":[3]}", false, 2, 0 },
	{ "deep", "{\"a\":{\"b\":[1,2,{\"c\":[3,4]}]}}", false, 1, 0 },
};

static data_for_each_cmd_t _count_nulls(const data_t *d, void *arg)
{
	size_t *nulls = arg;

	if (data_get_type(d) == DATA_TYPE_NULL)
		(*nulls)++;

	return DATA_FOR_EACH_CONT;
}

START_TEST(test_rfc_comma_positions)
{
	/*
	 * Every case here is strictly RFC 8259 conformant, so every plugin
	 * that serves application/json must accept it and produce the same
	 * structure. Running this for the RFC 8259 only grammar too is what
	 * makes the JSON6 result meaningful: it establishes the conformant
	 * baseline the extended grammar must not deviate from.
	 */
	for (int i = 0; i < ARRAY_SIZE(rfc_comma_cases); i++) {
		const char *tag = rfc_comma_cases[i].tag;
		const char *doc = rfc_comma_cases[i].doc;
		data_t *d = NULL;
		size_t nulls = 0;

		ck_assert_msg(!serialize_g_string_to_data(&d, doc, strlen(doc),
							  MIME_TYPE_JSON),
			      "%s: conformant document rejected: %s", tag, doc);

		if (rfc_comma_cases[i].is_list) {
			ck_assert_msg(data_get_type(d) == DATA_TYPE_LIST,
				      "%s: expected a list: %s", tag, doc);
			ck_assert_msg(data_get_list_length(d) ==
					      rfc_comma_cases[i].length,
				      "%s: expected %zu entries, got %zu: %s",
				      tag, rfc_comma_cases[i].length,
				      data_get_list_length(d), doc);

			data_list_for_each_const(d, _count_nulls, &nulls);
			ck_assert_msg(
				nulls == rfc_comma_cases[i].nulls,
				"%s: expected %zu null entries, got %zu: %s",
				tag, rfc_comma_cases[i].nulls, nulls, doc);
		} else {
			ck_assert_msg(data_get_type(d) == DATA_TYPE_DICT,
				      "%s: expected a dictionary: %s", tag,
				      doc);
			ck_assert_msg(data_get_dict_length(d) ==
					      rfc_comma_cases[i].length,
				      "%s: expected %zu keys, got %zu: %s", tag,
				      rfc_comma_cases[i].length,
				      data_get_dict_length(d), doc);
		}

		/* the document must also survive a dump and re-parse intact */
		_test_run(tag, d, MIME_TYPE_JSON, SER_FLAGS_COMPACT);

		FREE_NULL_DATA(d);
	}
}

END_TEST

START_TEST(test_utf8_roundtrip)
{
	const grammar_t grammar = _active_grammar();

	for (int i = 0; i < ARRAY_SIZE(utf8_cases); i++) {
		if ((utf8_cases[i].requires & grammar) !=
		    utf8_cases[i].requires) {
			debug("skipping %s: needs a grammar the bound plugin does not implement",
			      utf8_cases[i].tag);
			continue;
		}

		_test_utf8_case(utf8_cases[i].tag, utf8_cases[i].in,
				utf8_cases[i].in_bytes, utf8_cases[i].expect,
				utf8_cases[i].expect_bytes, SER_FLAGS_COMPACT);
		_test_utf8_case(utf8_cases[i].tag, utf8_cases[i].in,
				utf8_cases[i].in_bytes, utf8_cases[i].expect,
				utf8_cases[i].expect_bytes,
				(SER_FLAGS_COMPACT | SER_FLAGS_JSON6));
	}
}

END_TEST

/* Run every test against the one implementation selected by setup_fn */
static void _add_tcase(Suite *suite, const char *name, SFun setup_fn)
{
	TCase *tcase = tcase_create(name);

	/*
	 * Checked, not unchecked: the fixture has to run inside the forked
	 * child of each test. serializer_g_init() is one shot per process --
	 * serializer_g_fini() only releases the plugins under
	 * MEMORY_LEAK_DEBUG, and it sets a flag that makes a later
	 * serializer_g_init() assert. An unchecked fixture runs in the parent,
	 * so the second tcase would abort the runner before it could bind its
	 * plugin.
	 */
	tcase_add_checked_fixture(tcase, setup_fn, teardown);
	/* generous timeout: the bandwidth and large-dataset cases run a while */
	tcase_set_timeout(tcase, 3000);

	tcase_add_test(tcase, test_mimetype);
	tcase_add_test(tcase, test_parse_invalid);
	tcase_add_test(tcase, test_parse_valid);
	tcase_add_test(tcase, test_rfc_comma_positions);
	tcase_add_test(tcase, test_utf8_roundtrip);
	tcase_add_test(tcase, test_compliance_large);
	tcase_add_test(tcase, test_bandwidth);

	suite_add_tcase(suite, tcase);
}

/* True if plugin should run: no filter set, or the filter names it */
static bool _selected(const char *only, const char *plugin)
{
	if (!only || !only[0])
		return true;

	return !xstrcmp(_bare_plugin(only), _bare_plugin(plugin));
}

extern int main(int argc, char **argv)
{
	int failures, added = 0;
	Suite *suite = suite_create("serializer");
	SRunner *sr = NULL;
	const char *only = getenv(SERIALIZER_ENV);

	/*
	 * Run every test once per implementation that serves
	 * application/json. Both must satisfy the RFC 8259 cases; the cases
	 * that need JSON6 gate themselves on _active_grammar(), so they run
	 * only under serializer/xjson.
	 *
	 * A single suite holds both tcases because the runner writes one XML
	 * document and the harness reads a single <suite> from it.
	 *
	 * SLURM_TESTSUITE_SERIALIZER limits the run to one implementation,
	 * accepting it with or without the "serializer/" prefix the same way
	 * load_plugins() does.
	 */
#ifdef HAVE_JSON
	/* serializer/json is only built when a JSON parser library was found */
	if (_selected(only, MIME_TYPE_JSON_PLUGIN)) {
		_add_tcase(suite, MIME_TYPE_JSON_PLUGIN, _setup_json);
		added++;
	}
#endif

	if (_selected(only, MIME_TYPE_XJSON_PLUGIN)) {
		_add_tcase(suite, MIME_TYPE_XJSON_PLUGIN, _setup_xjson);
		added++;
	}

	if (!added) {
		/*
		 * Running nothing would report success, so refuse instead of
		 * silently proving nothing.
		 */
		fprintf(stderr, "%s=\"%s\" selected no serializer plugin\n",
			SERIALIZER_ENV, (only ? only : ""));
		return 1;
	}

	sr = srunner_create(suite);

	/*
	 * Each tcase binds a different plugin, and serializer_g_init() is one
	 * shot per process, so every test must run in its own fork. CK_FORK=no
	 * would leave the second tcase silently bound to the first tcase's
	 * plugin. Force forking rather than testing the wrong implementation.
	 */
	srunner_set_fork_status(sr, CK_FORK);

	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failures;
}
