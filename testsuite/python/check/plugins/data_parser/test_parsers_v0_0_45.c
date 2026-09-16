/******************************************************************************
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Test every parser in the data_parser/v0.0.45 parsers[] table.
 *****************************************************************************/

/*
 * One libcheck test unit per parser in the data_parser/v0.0.45 parsers[]
 * table. These checks are the former in-plugin check_parser_funcname()
 * xassert() sweep, re-homed here. A test unit per parser means the failing
 * parser is identified by its test iteration, and every other parser is still
 * checked afterwards.
 *
 * Every data_parser version keeps its own copy of this test, the same way it
 * keeps its own copy of the plugin the test checks. Keep this file in step
 * with src/plugins/data_parser/v0.0.45/, not with the other versions.
 */

#define DATA_PARSER_PLUGIN_TYPE "data_parser/v0.0.45"
/*
 * plugin_load_and_link() appends ".so" and maps only '/' to '_', never '.', so
 * the load name must already be underscored to find data_parser_v0_0_45.so.
 * Do not "fix" this to match DATA_PARSER_PLUGIN_TYPE.
 */
#define DATA_PARSER_PLUGIN_NAME "data_parser/v0_0_45"

#include <check.h>
#include <stdlib.h>

/* provides SLURM_BIT() used by the plugin's api.h */
#include "slurm/slurm.h"

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/read_config.h"
#include "src/common/xstring.h"

#include "src/plugins/data_parser/v0.0.45/parsers.h"

/* must match get_parsers() from the versioned parsers.h */
typedef void (*get_parsers_func_t)(const parser_t **parsers_ptr,
				   int *count_ptr);

static const parser_t *parsers = NULL;
static int parser_count = 0;
static bool plugin_loaded = true;

/*
 * Replacement for the plugin's find_parser_by_type().
 *
 * Only get_parsers() is resolved from the plugin, so do the same linear scan
 * over the loaded parser table here. Like the plugin, this returns NULL for a
 * type that is not in the table.
 */
static const parser_t *_find_parser_by_type(type_t type)
{
	for (int i = 0; i < parser_count; i++)
		if (parsers[i].type == type)
			return &parsers[i];

	return NULL;
}

static void _check_flag_bit(int8_t i, const flag_bit_t *bit, bool *found_bit,
			    ssize_t parser_size)
{
	ck_assert(bit->magic == MAGIC_FLAG_BIT);
	ck_assert(bit->type > FLAG_BIT_TYPE_INVALID);
	ck_assert(bit->type < FLAG_BIT_TYPE_MAX);
	ck_assert(bit->name && bit->name[0]);

	if (bit->type == FLAG_BIT_TYPE_REMOVED) {
		ck_assert(!bit->mask_size);
		ck_assert(!bit->mask_name);
		ck_assert(!bit->value);
		ck_assert(!bit->flag_name);
		ck_assert(!bit->flag_size);
		ck_assert(bit->deprecated);
		return;
	}

	/* mask must be set */
	ck_assert(bit->mask);
	ck_assert(bit->flag_size <= sizeof(bit->value));
	ck_assert(bit->flag_size > 0);
	ck_assert(bit->flag_name && bit->flag_name[0]);
	ck_assert(bit->mask_size <= sizeof(bit->value));
	ck_assert(bit->mask_size > 0);
	ck_assert(bit->mask_name && bit->mask_name[0]);

	/* Bit values must fit in parser->size bits */
	switch (parser_size) {
	case sizeof(uint8_t):
		ck_assert((bit->value & UINT8_MAX) == bit->value);
		break;
	case sizeof(uint16_t):
		ck_assert((bit->value & UINT16_MAX) == bit->value);
		break;
	case sizeof(uint32_t):
		ck_assert((bit->value & UINT32_MAX) == bit->value);
		break;
	case sizeof(uint64_t):
		ck_assert((bit->value & UINT64_MAX) == bit->value);
		break;
	default:
		ck_abort_msg(
			"Parser->size (%zd) is invalid. This should never happen.",
			parser_size);
	}

	if (bit->type == FLAG_BIT_TYPE_BIT) {
		/* at least one bit must be set */
		ck_assert(bit->value);
		/* mask must include all value bits */
		ck_assert((bit->mask & bit->value) == bit->value);
		*found_bit = true;
	} else if (bit->type == FLAG_BIT_TYPE_EQUAL) {
		/*
		 * bit->mask must include all value bits
		 * (if there are any)
		 */
		ck_assert(!bit->value ||
			  ((bit->mask & bit->value) == bit->value));
		/*
		 * All equal type flags should come before any bit
		 * type flags to avoid issues with masks overlapping
		 * except for hidden values
		 */
		ck_assert(bit->hidden || !*found_bit);
	}
}

static void _check_parser(const parser_t *const parser)
{
	ck_assert(parser->magic == MAGIC_PARSER);

	ck_assert(parser->model > PARSER_MODEL_INVALID);
	ck_assert(parser->model < PARSER_MODEL_MAX);

	if (parser->model == PARSER_MODEL_REMOVED) {
		ck_assert(parser->deprecated > 0);
		ck_assert(parser->obj_openapi > OPENAPI_FORMAT_INVALID);
		ck_assert(parser->obj_openapi < OPENAPI_FORMAT_MAX);
		ck_assert(!parser->size);
		ck_assert(!parser->field_name);
		ck_assert(parser->ptr_offset == NO_VAL);
		ck_assert(!parser->key);
		ck_assert(!parser->flag_bit_array_count);
		ck_assert(!parser->type_string);
		ck_assert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		ck_assert(!parser->fields);
		ck_assert(!parser->field_count);
		ck_assert(!parser->parse);
		ck_assert(!parser->dump);
		ck_assert(!parser->pointer_type);
		ck_assert(!parser->array_type);
		return;
	}

	ck_assert(parser->obj_type_string && parser->obj_type_string[0]);

	if (parser->model == PARSER_MODEL_ALIAS) {
		ck_assert(!parser->size);
		ck_assert(!parser->field_name);
		ck_assert(parser->ptr_offset == NO_VAL);
		ck_assert(!parser->key);
		ck_assert(!parser->deprecated);
		ck_assert(!parser->flag_bit_array_count);
		ck_assert(parser->type_string && parser->type_string[0]);
		ck_assert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		ck_assert(!parser->fields);
		ck_assert(!parser->field_count);
		ck_assert(!parser->parse);
		ck_assert(!parser->dump);
		ck_assert(!parser->pointer_type);
		ck_assert(!parser->array_type);
		ck_assert(parser->obj_openapi == OPENAPI_FORMAT_INVALID);
		ck_assert(parser->alias_type > DATA_PARSER_TYPE_INVALID);
		ck_assert(parser->alias_type < DATA_PARSER_TYPE_MAX);
		ck_assert(parser->alias_type != parser->type);
		return;
	}

	ck_assert(parser->alias_type == DATA_PARSER_TYPE_INVALID);

	if (parser->model == PARSER_MODEL_ARRAY_REMOVED_FIELD) {
		ck_assert(!parser->size);
		ck_assert(!parser->field_name);
		ck_assert(parser->ptr_offset == NO_VAL);
		ck_assert(parser->key && parser->key[0]);
		ck_assert(parser->deprecated);
		ck_assert(!parser->flag_bit_array_count);
		ck_assert(parser->type_string && parser->type_string[0]);
		ck_assert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		ck_assert(!parser->fields);
		ck_assert(!parser->field_count);
		ck_assert(!parser->parse);
		ck_assert(!parser->dump);
		ck_assert(!parser->pointer_type);
		ck_assert(!parser->array_type);
		ck_assert(parser->obj_openapi == OPENAPI_FORMAT_INVALID);
		return;
	}

	ck_assert(parser->size > 0);

	if (parser->model == PARSER_MODEL_ARRAY_SKIP_FIELD) {
		/* field is only a place holder so most assert()s don't apply */
		ck_assert(parser->field_name && parser->field_name[0]);
		ck_assert(parser->type == DATA_PARSER_TYPE_INVALID);
		ck_assert(!parser->flag_bit_array_count);
		ck_assert(parser->needs == NEED_NONE);
		ck_assert(!parser->field_name_overloads);
		ck_assert(!parser->key);
		ck_assert(!parser->type_string);
		ck_assert(!parser->required);
		ck_assert((parser->ptr_offset < NO_VAL) ||
			  (parser->ptr_offset >= 0));
		return;
	}

	ck_assert(parser->type > DATA_PARSER_TYPE_INVALID);
	ck_assert(parser->type < DATA_PARSER_TYPE_MAX);
	ck_assert(parser->type_string && parser->type_string[0]);

	if (parser->model == PARSER_MODEL_FLAG_ARRAY) {
		bool found_bit_type = false;

		/* parser of a specific flag field list */
		ck_assert(parser->flag_bit_array);
		ck_assert(parser->flag_bit_array_count < NO_VAL8);

		for (int8_t i = 0; i < parser->flag_bit_array_count; i++) {
			_check_flag_bit(i, &parser->flag_bit_array[i],
					&found_bit_type, parser->size);

			/* check for duplicate flag names */
			for (int j = 0; j < parser->flag_bit_array_count; j++) {
				ck_assert((i == j) ||
					  xstrcasecmp(parser->flag_bit_array[i]
							      .name,
						      parser->flag_bit_array[j]
							      .name));
			}
		}

		/* make sure this is not a list or array type */
		ck_assert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		ck_assert(!parser->fields);
		ck_assert(!parser->field_count);
		ck_assert(!parser->parse);
		ck_assert(!parser->dump);
		ck_assert(parser->ptr_offset == NO_VAL);
		ck_assert(!parser->pointer_type);
		ck_assert(!parser->array_type);
		ck_assert(parser->obj_openapi == OPENAPI_FORMAT_ARRAY);
	} else if (parser->model == PARSER_MODEL_LIST) {
		/* parser of a List */
		ck_assert(parser->list_type > DATA_PARSER_TYPE_INVALID);
		ck_assert(parser->list_type < DATA_PARSER_TYPE_MAX);
		ck_assert(!parser->flag_bit_array_count);
		ck_assert(!parser->fields);
		ck_assert(!parser->field_count);
		ck_assert(!parser->parse);
		ck_assert(!parser->dump);
		ck_assert(parser->size == sizeof(list_t *));
		ck_assert(parser->ptr_offset == NO_VAL);
		ck_assert(!parser->pointer_type);
		ck_assert(!parser->array_type);
		ck_assert(!parser->obj_openapi);
	} else if (parser->model == PARSER_MODEL_ARRAY) {
		/* parser of a parser Array */
		ck_assert(parser->field_count > 0);

		ck_assert(parser->ptr_offset == NO_VAL);
		ck_assert(!parser->flag_bit_array_count);
		ck_assert(!parser->parse);
		ck_assert(!parser->dump);
		ck_assert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		ck_assert(parser->fields);
		ck_assert(!parser->pointer_type);
		ck_assert(!parser->array_type);
		ck_assert(parser->obj_openapi == OPENAPI_FORMAT_OBJECT);

		for (int i = 0; i < parser->field_count; i++) {
			/* recursively check the child parsers */
			_check_parser(&parser->fields[i]);

			/*
			 * Verify each field_name is unique while ignoring
			 * complex parsers.
			 */
			if (parser->fields[i].field_name) {
				int matches = 0;

				for (int j = 0; j < parser->field_count; j++) {
					if (i == j)
						continue;

					if (!xstrcasecmp(parser->fields[i]
								 .field_name,
							 parser->fields[j]
								 .field_name))
						matches++;
				}

				ck_assert(matches ==
					  parser->fields[i]
						  .field_name_overloads);
			}

			/*
			 * Verify each key path is unique while ignoring skipped
			 * parsers
			 */
			if (parser->fields[i].key)
				for (int j = 0; j < parser->field_count; j++)
					ck_assert((i == j) ||
						  xstrcasecmp(parser->fields[i]
								      .key,
							      parser->fields[j]
								      .key));
		}
	} else if ((parser->model == PARSER_MODEL_ARRAY_LINKED_FIELD) ||
		   (parser->model ==
		    PARSER_MODEL_ARRAY_LINKED_EXPLODED_FLAG_ARRAY_FIELD)) {
		/* parser array link to a another parser */
		const parser_t *const linked =
			_find_parser_by_type(parser->type);

		if (parser->model !=
		    PARSER_MODEL_ARRAY_LINKED_EXPLODED_FLAG_ARRAY_FIELD) {
			ck_assert(parser->key && parser->key[0]);
		}

		ck_assert(!parser->flag_bit_array_count);
		ck_assert(!parser->fields);
		ck_assert(!parser->field_count);
		ck_assert(!parser->parse);
		ck_assert(!parser->dump);
		ck_assert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		ck_assert(!parser->pointer_type);
		ck_assert(!parser->array_type);
		ck_assert(!parser->obj_openapi);

		switch (linked->model) {
		case PARSER_MODEL_ALIAS:
			ck_assert(linked->alias_type >
				  DATA_PARSER_TYPE_INVALID);
			ck_assert(linked->alias_type < DATA_PARSER_TYPE_MAX);
			ck_assert(linked->alias_type != parser->type);
			break;
		case PARSER_MODEL_REMOVED:
			ck_abort_msg("should never execute");
		case PARSER_MODEL_SIMPLE:
			ck_assert(parser->field_name && parser->field_name[0]);
			/* fall through */
		case PARSER_MODEL_ARRAY:
		case PARSER_MODEL_FLAG_ARRAY:
		case PARSER_MODEL_LIST:
		case PARSER_MODEL_PTR:
		case PARSER_MODEL_NT_ARRAY:
		case PARSER_MODEL_NT_PTR_ARRAY:
			/* linked parsers must always be the same size */
			ck_assert((parser->size == NO_VAL) ||
				  (parser->size == linked->size));
			ck_assert((parser->ptr_offset < NO_VAL) ||
				  (parser->ptr_offset >= 0));
			break;
		case PARSER_MODEL_COMPLEX:
			ck_assert(!parser->field_name);
			/*
			 * complex uses the size of the struct which we don't
			 * know here
			 */
			ck_assert(parser->size > 0);
			ck_assert(parser->size <= NO_VAL);
			ck_assert(parser->ptr_offset == NO_VAL);
			break;
		case PARSER_MODEL_ARRAY_LINKED_FIELD:
		case PARSER_MODEL_ARRAY_LINKED_EXPLODED_FLAG_ARRAY_FIELD:
			ck_abort_msg(
				"linked parsers must not link to other linked parsers");
		case PARSER_MODEL_ARRAY_SKIP_FIELD:
			ck_abort_msg(
				"linked parsers must not link to a skip parsers");
		case PARSER_MODEL_ARRAY_REMOVED_FIELD:
			ck_abort_msg(
				"linked parsers must not link to a removed parser");
		case PARSER_MODEL_INVALID:
		case PARSER_MODEL_MAX:
			ck_abort_msg("invalid model");
		}
	} else if ((parser->model == PARSER_MODEL_SIMPLE) ||
		   (parser->model == PARSER_MODEL_COMPLEX)) {
		ck_assert(parser->ptr_offset == NO_VAL);
		ck_assert(!parser->key);
		ck_assert(!parser->field_name);
		ck_assert(!parser->flag_bit_array_count);
		ck_assert(!parser->fields);
		ck_assert(!parser->field_count);
		ck_assert(parser->parse);
		ck_assert(parser->dump);
		ck_assert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		if ((parser->obj_openapi == OPENAPI_FORMAT_ARRAY) ||
		    (parser->obj_openapi == OPENAPI_FORMAT_OBJECT) ||
		    (parser->obj_openapi == OPENAPI_FORMAT_INVALID)) {
			/*
			 * Only one of the overrides is allowed but one must be
			 * set
			 */
			if (parser->array_type) {
				ck_assert(!parser->pointer_type);
			} else if (parser->pointer_type) {
				ck_assert(!parser->array_type);
			} else if (!parser->field_name) {
				/* field-less parser can can be any type */
			} else {
				ck_abort_msg("invalid openapi override");
			}
		} else {
			ck_assert(parser->obj_openapi > OPENAPI_FORMAT_INVALID);
			ck_assert(parser->obj_openapi < OPENAPI_FORMAT_MAX);
			ck_assert(!parser->pointer_type);
			ck_assert(!parser->array_type);
		}
	} else if (parser->model == PARSER_MODEL_PTR) {
		ck_assert(parser->pointer_type > DATA_PARSER_TYPE_INVALID);
		ck_assert(parser->pointer_type < DATA_PARSER_TYPE_MAX);
		ck_assert(parser->size == sizeof(void *));
		ck_assert(parser->ptr_offset == NO_VAL);
		ck_assert(!parser->field_name);
		ck_assert(!parser->key);
		ck_assert(!parser->field_name);
		ck_assert(!parser->flag_bit_array_count);
		ck_assert(!parser->fields);
		ck_assert(!parser->field_count);
		ck_assert(!parser->parse);
		ck_assert(!parser->dump);
		ck_assert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		ck_assert(!parser->array_type);
		ck_assert(!parser->obj_openapi);
	} else if ((parser->model == PARSER_MODEL_NT_ARRAY) ||
		   (parser->model == PARSER_MODEL_NT_PTR_ARRAY)) {
		if (parser->model == PARSER_MODEL_NT_PTR_ARRAY) {
			const parser_t *const eparser =
				_find_parser_by_type(parser->array_type);
			ck_assert(eparser->pointer_type);
		}

		ck_assert(!parser->pointer_type);
		ck_assert(parser->array_type > DATA_PARSER_TYPE_INVALID);
		ck_assert(parser->array_type < DATA_PARSER_TYPE_MAX);
		ck_assert(parser->size == sizeof(void *));
		ck_assert(parser->ptr_offset == NO_VAL);
		ck_assert(!parser->field_name);
		ck_assert(!parser->key);
		ck_assert(!parser->field_name);
		ck_assert(!parser->flag_bit_array_count);
		ck_assert(!parser->fields);
		ck_assert(!parser->field_count);
		ck_assert(!parser->parse);
		ck_assert(!parser->dump);
		ck_assert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		ck_assert(!parser->obj_openapi);
	} else {
		ck_abort_msg("invalid parser model %u", parser->model);
	}
}

START_TEST(test_parsers_loaded)
{
	ck_assert(plugin_loaded);
	ck_assert(parsers);
	ck_assert(parser_count > 0);
}

END_TEST

START_TEST(test_parser)
{
	_check_parser(&parsers[_i]);
}

END_TEST

extern int main(int argc, char **argv)
{
	int failures;
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");
	const char *syms[] = { "get_parsers" };
	get_parsers_func_t funcs[1] = { NULL };
	plugin_handle_t plug = PLUGIN_INVALID_HANDLE;
	TCase *tcase = NULL;
	Suite *suite = NULL;
	SRunner *sr = NULL;

	/* Setup logging */
	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	log_init("data_parser-parsers-test", log_opts, 0, NULL);

	/* slurm_conf_init() is what populates slurm_conf.plugindir */
	if (slurm_conf_init(NULL)) {
		error("slurm_conf_init() failed");
		plugin_loaded = false;
	}

	/*
	 * slurm_conf_init() always overwrites slurm_conf.debug_flags, so the
	 * environment override is only effective once the conf is loaded.
	 */
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);

	/*
	 * plugin_load_and_link() appends ".so" and only maps '/' to '_' (it
	 * never maps '.'), so it must be handed the underscored plugin name to
	 * find data_parser_v0_0_XX.so. DATA_PARSER_PLUGIN_TYPE keeps the
	 * human readable "data_parser/v0.0.XX" for the logs. Do not "fix" the
	 * underscored name: the dotted one always fails to load.
	 */
	if (plugin_loaded) {
		plug = plugin_load_and_link(DATA_PARSER_PLUGIN_NAME,
					    ARRAY_SIZE(syms), syms,
					    (void **) funcs);

		if (plug == PLUGIN_INVALID_HANDLE) {
			error("Unable to load %s plugin (%s) from %s",
			      DATA_PARSER_PLUGIN_TYPE, DATA_PARSER_PLUGIN_NAME,
			      slurm_conf.plugindir);
			plugin_loaded = false;
		}
	}

	/*
	 * A load failure must still fall through to srunner_run_all(): the
	 * caller only reads the XML results file, which is never written if the
	 * suite does not run. test_parsers_loaded() reports the failure and
	 * tcase_add_loop_test() with a bound of 0 simply adds no tests.
	 */
	if (plugin_loaded)
		funcs[0](&parsers, &parser_count);

	/*
	 * The suite can only be built once parser_count is known as
	 * tcase_add_loop_test() needs it, which is why the plugin is not
	 * loaded from a libcheck fixture.
	 */
	tcase = tcase_create("parsers");
	tcase_add_test(tcase, test_parsers_loaded);
	tcase_add_loop_test(tcase, test_parser, 0, parser_count);

	suite = suite_create(DATA_PARSER_PLUGIN_TYPE);
	suite_add_tcase(suite, tcase);

	/* Create and run the runner */
	sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	plugin_unload(plug);
	slurm_conf_destroy();
	log_fini();

	return failures;
}
