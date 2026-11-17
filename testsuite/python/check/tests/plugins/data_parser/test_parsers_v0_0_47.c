/******************************************************************************
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Test every parser in the data_parser/v0.0.47 parsers[] table.
 *****************************************************************************/

/*
 * One libcheck test unit per parser in the data_parser/v0.0.47 parsers[]
 * table, checking the parser_t invariants that every entry must satisfy. A
 * test unit per parser means the failing parser is identified by its test
 * iteration, and every other parser is still checked afterwards.
 *
 * The deprecated field cases are not invariant checks:
 * they call the plugin's parse() to pin when a deprecated field warns, and
 * that the warning points the client at that field and not another one. Each
 * one scans the table for a field of the shape it needs, so the table decides
 * what they run.
 *
 * Every data_parser version keeps its own copy of this test, the same way it
 * keeps its own copy of the plugin the test checks. Keep this file in step
 * with src/plugins/data_parser/v0.0.47/, not with the other versions.
 */

#define DATA_PARSER_PLUGIN_TYPE "data_parser/v0.0.47"
/*
 * plugin_load_and_link() appends ".so" and maps only '/' to '_', never '.', so
 * the load name must already be underscored to find data_parser_v0_0_47.so.
 * Do not "fix" this to match DATA_PARSER_PLUGIN_TYPE.
 */
#define DATA_PARSER_PLUGIN_NAME "data_parser/v0_0_47"

#include <check.h>
#include <stdarg.h>
#include <stdlib.h>

/* provides SLURM_BIT() used by the plugin's api.h */
#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/openapi.h"
#include "src/common/plugin.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/serializer.h"

#include "src/plugins/data_parser/v0.0.47/parsers.h"

/* must match get_parsers() from the versioned parsers.h */
typedef void (*get_parsers_func_t)(const parser_t **parsers_ptr,
				   int *count_ptr);

/* must match parse() from the versioned parsing.h */
typedef int (*parse_func_t)(void *dst, ssize_t dst_bytes,
			    const parser_t *const parser, data_t *src,
			    args_t *args, data_t *parent_path);

static const parser_t *parsers = NULL;
static int parser_count = 0;
static bool plugin_loaded = true;
static parse_func_t parse_func = NULL;

/*
 * Deprecated field warning test variables
 * All deprecated warnings must name the tested field as the source
 */
static char *expected_warn_source = NULL;
static char *bad_warn_source = NULL;
static int deprecated_warn_count = 0;
static int bad_source_warn_count = 0;

/*
 * Replacement for the plugin's find_parser_by_type().
 *
 * Only get_parsers() is resolved from the plugin, so do the same linear scan
 * over the loaded parser table here. Like the plugin, this returns NULL for a
 * type that is not in the table, so every caller must check the result.
 */
static const parser_t *_find_parser_by_type(type_t type)
{
	for (int i = 0; i < parser_count; i++)
		if (parsers[i].type == type)
			return &parsers[i];

	return NULL;
}

static void _check_flag_bit(const flag_bit_t *bit, bool *found_bit,
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
		ck_assert(_find_parser_by_type(parser->alias_type));
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

		for (int i = 0; i < parser->flag_bit_array_count; i++) {
			_check_flag_bit(&parser->flag_bit_array[i],
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
		ck_assert(_find_parser_by_type(parser->list_type));
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

		ck_assert(linked);

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
		ck_assert(_find_parser_by_type(parser->pointer_type));
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

			ck_assert(eparser);
			ck_assert(eparser->pointer_type);
		}

		ck_assert(!parser->pointer_type);
		ck_assert(parser->array_type > DATA_PARSER_TYPE_INVALID);
		ck_assert(parser->array_type < DATA_PARSER_TYPE_MAX);
		ck_assert(_find_parser_by_type(parser->array_type));
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

__attribute__((format(printf, 4, 5))) static void _count_deprecated_warn(
	void *arg, data_parser_type_t type, const char *source, const char *why,
	...)
{
	char *msg = NULL;
	va_list ap;

	va_start(ap, why);
	msg = vxstrfmt(why, ap);
	va_end(ap);

	/* every other kind of warning is noise for this test */
	if (xstrstr(msg, "is deprecated")) {
		if (xstrcmp(source, expected_warn_source)) {
			bad_source_warn_count++;

			if (!bad_warn_source)
				bad_warn_source = xstrdup(source);
		} else {
			deprecated_warn_count++;
		}
	}

	xfree(msg);
}

/*
 * A value that does not parse is not what is being tested here: the warning is
 * emitted before the field's own parser runs, so let the parse continue.
 *
 * A missing required field is the one error that must stop the parse. The
 * plugin hands the field's parser a NULL value when the caller asks to carry
 * on, which a real caller never does: it fails the request instead.
 */
__attribute__((format(printf, 5, 6))) static bool _ignore_error(
	void *arg, data_parser_type_t type, int error_code, const char *source,
	const char *why, ...)
{
	return (error_code != ESLURM_DATA_PATH_NOT_FOUND);
}

/*
 * Check whether parsing this field would load a prerequisite.
 *
 * A linked field always carries NEED_NONE itself: the add_parser* macros
 * hardcode it. The prerequisite belongs to the type the field links to, which
 * is the parser that parse() calls load_prereqs() for. A type that is not in
 * the table counts as needing one, as there is no parser to ask.
 */
static bool _needs_prereqs(const parser_t *const field)
{
	const parser_t *const type = _find_parser_by_type(field->type);

	return !type || (type->needs != NEED_NONE);
}

/*
 * A field only reaches the deprecated warning when it is a linked field whose
 * deprecation is at or below the oldest supported protocol version. A field
 * whose type has a prerequisite is skipped: loading one needs a database
 * connection that this test does not have.
 */
static bool _is_warning_candidate(const parser_t *const field)
{
	if (field->model != PARSER_MODEL_ARRAY_LINKED_FIELD)
		return false;

	if (!field->deprecated ||
	    (field->deprecated > SLURM_MIN_PROTOCOL_VERSION))
		return false;

	return field->key && !_needs_prereqs(field);
}

/*
 * Check whether the array can be parsed without loading a prerequisite.
 *
 * Every required field is given a value, so a required field whose type has a
 * prerequisite would load one even though it is not the field under test.
 */
static bool _is_testable_array(const parser_t *const array)
{
	for (int i = 0; i < array->field_count; i++) {
		const parser_t *const field = &array->fields[i];

		if (field->required && field->key && _needs_prereqs(field))
			return false;
	}

	return true;
}

/*
 * Find the replacement that writes the same struct field as the deprecated
 * field.
 *
 * A replacement that is itself deprecated would add a warning of its own and
 * hide the one being counted, so skip those.
 */
static const parser_t *_find_sibling(const parser_t *const array,
				     const parser_t *const field)
{
	for (int i = 0; i < array->field_count; i++) {
		const parser_t *const sibling = &array->fields[i];

		if ((sibling == field) || !sibling->key ||
		    sibling->deprecated ||
		    xstrcasecmp(sibling->field_name, field->field_name))
			continue;

		return sibling;
	}

	return NULL;
}

/*
 * Find a deprecated field that no other field shares.
 *
 * The warning for one of these can never be suppressed, as there is no
 * replacement to hold the same value.
 */
static bool _find_deprecated_alone(const parser_t **array_ptr,
				   const parser_t **field_ptr)
{
	for (int i = 0; i < parser_count; i++) {
		const parser_t *const array = &parsers[i];

		if ((array->model != PARSER_MODEL_ARRAY) ||
		    !_is_testable_array(array))
			continue;

		for (int j = 0; j < array->field_count; j++) {
			const parser_t *const field = &array->fields[j];

			if (!_is_warning_candidate(field) ||
			    field->field_name_overloads)
				continue;

			*array_ptr = array;
			*field_ptr = field;
			return true;
		}
	}

	return false;
}

/*
 * Find a deprecated field that shares its struct field with a replacement.
 *
 * Only such a field reaches the comparison that decides whether the warning is
 * suppressed.
 */
static bool _find_deprecated_overloaded(const parser_t **array_ptr,
					const parser_t **field_ptr,
					const parser_t **sibling_ptr)
{
	for (int i = 0; i < parser_count; i++) {
		const parser_t *const array = &parsers[i];

		if ((array->model != PARSER_MODEL_ARRAY) ||
		    !_is_testable_array(array))
			continue;

		for (int j = 0; j < array->field_count; j++) {
			const parser_t *const field = &array->fields[j];
			const parser_t *sibling = NULL;

			if (!_is_warning_candidate(field) ||
			    !field->field_name_overloads)
				continue;

			if (!(sibling = _find_sibling(array, field)))
				continue;

			*array_ptr = array;
			*field_ptr = field;
			*sibling_ptr = sibling;
			return true;
		}
	}

	return false;
}

/*
 * Parse a dictionary holding the tested field, and key2 when it is given, and
 * return how many times the deprecated warning fired for the tested field.
 *
 * With echo set, every other field sharing the tested field's struct field
 * holds the same value too, as they all do in a dumped object.
 *
 * Only a warning that names the tested field and carries its source path is
 * counted.
 *
 * Whatever the parsers allocated inside dst is leaked: an array parser carries
 * no destructor for the object it fills, and the object type is only known at
 * runtime here.
 */
static int _count_warnings(const parser_t *const array,
			   const parser_t *const tested, const char *value,
			   bool echo, const char *key2, const char *value2)
{
	args_t args = {
		.magic = MAGIC_ARGS,
		.on_parse_error = _ignore_error,
		.on_parse_warn = _count_deprecated_warn,
	};
	data_t *src = data_set_dict(data_new());
	data_t *parent_path = data_set_list(data_new());
	data_t *field_path = NULL;
	void *dst = xmalloc(array->size);

	deprecated_warn_count = 0;
	bad_source_warn_count = 0;
	xfree(bad_warn_source);

	/*
	 * A required field that is absent stops the parse, which may happen
	 * before the field under test is reached, so give every required field
	 * a value. Only the deprecated warning is counted, so whether these
	 * values parse does not matter.
	 */
	for (int i = 0; i < array->field_count; i++) {
		const parser_t *const field = &array->fields[i];

		if (field->required && field->key)
			data_set_string(data_define_dict_path(src, field->key),
					"1");
	}

	data_set_string(data_define_dict_path(src, tested->key), value);

	for (int i = 0; echo && (i < array->field_count); i++) {
		const parser_t *const field = &array->fields[i];

		/* the same fields the plugin compares the tested field against */
		if ((field == tested) || !field->key ||
		    xstrcasecmp(field->field_name, tested->field_name))
			continue;

		data_set_string(data_define_dict_path(src, field->key), value);
	}

	if (key2)
		data_set_string(data_define_dict_path(src, key2), value2);

	data_set_string(data_list_append(parent_path), __func__);

	/*
	 * Build the source path the same way the plugin does, so the test
	 * pins the path a client is given rather than a copy of the rule that
	 * makes it.
	 */
	ck_assert_int_eq(data_clone(parent_path, field_path), SLURM_SUCCESS);
	openapi_append_rel_path(field_path, tested->key);
	xfree(expected_warn_source);
	openapi_fmt_rel_path_str(&expected_warn_source, field_path);
	FREE_NULL_DATA(field_path);

	parse_func(dst, NO_VAL, array, src, &args, parent_path);

	xfree(dst);
	FREE_NULL_DATA(parent_path);
	FREE_NULL_DATA(src);

	/*
	 * A warning that names the field but points somewhere else sends the
	 * client to the wrong field, so it is a failure and not a miscount.
	 */
	ck_assert_msg(
		!bad_source_warn_count,
		"%s: deprecated warning for \"%s\" carried source \"%s\", expected \"%s\"",
		array->obj_type_string, tested->key, bad_warn_source,
		expected_warn_source);

	return deprecated_warn_count;
}

/*
 * Each case below scans for a field matching the shape it tests.
 *
 * A case that finds nothing reports that and checks nothing. It starts
 * exercising the plugin on its own as soon as a field of its shape is added to
 * the table, with no change needed here.
 */
START_TEST(test_deprecated_field_alone)
{
	const parser_t *array = NULL, *field = NULL;
	int count;

	ck_assert(plugin_loaded);
	ck_assert(parse_func);

	if (!_find_deprecated_alone(&array, &field)) {
		info("%s: no deprecated field without a replacement",
		     DATA_PARSER_PLUGIN_TYPE);
		return;
	}
	count = _count_warnings(array, field, "1", false, NULL, NULL);

	ck_assert_msg(count == 1,
		      "%s: expected 1 deprecated warning for \"%s\", got %d",
		      array->obj_type_string, field->key, count);
}

END_TEST

START_TEST(test_deprecated_field_overloaded)
{
	const parser_t *array = NULL, *field = NULL, *sibling = NULL;
	int count;

	ck_assert(plugin_loaded);
	ck_assert(parse_func);

	if (!_find_deprecated_overloaded(&array, &field, &sibling)) {
		info("%s: no deprecated field with a replacement",
		     DATA_PARSER_PLUGIN_TYPE);
		return;
	}

	/* the same value under every key means the dump was echoed back */
	count = _count_warnings(array, field, "1", true, NULL, NULL);
	ck_assert_msg(
		!count,
		"%s: \"%s\" warned %d time(s) while every field sharing it held the same value",
		array->obj_type_string, field->key, count);

	/* a value of its own means the client asked for the deprecated key */
	count = _count_warnings(array, field, "1", false, sibling->key, "2");
	ck_assert_msg(
		count == 1,
		"%s: expected 1 deprecated warning for \"%s\" while \"%s\" held another value, got %d",
		array->obj_type_string, field->key, sibling->key, count);

	/*
	 * With more than one replacement, matching one of them is not enough: a
	 * dump holds the same value under every key. Required fields hold "1",
	 * so use another value to keep every other sibling different or absent.
	 */
	if (field->field_name_overloads > 1) {
		count = _count_warnings(array, field, "2", false, sibling->key,
					"2");
		ck_assert_msg(
			count == 1,
			"%s: expected 1 deprecated warning for \"%s\" while only \"%s\" held the same value, got %d",
			array->obj_type_string, field->key, sibling->key,
			count);
	}

	/*
	 * A required replacement cannot be left out: the parse stops at the
	 * missing field, which may come before the deprecated one.
	 */
	if (sibling->required) {
		info("%s: \"%s\" is required, skipping the absent case",
		     array->obj_type_string, sibling->key);
		return;
	}

	/* nothing to compare against must warn, not stay silent */
	count = _count_warnings(array, field, "1", false, NULL, NULL);
	ck_assert_msg(
		count == 1,
		"%s: expected 1 deprecated warning for \"%s\" while \"%s\" was absent, got %d",
		array->obj_type_string, field->key, sibling->key, count);
}

END_TEST

extern int main(int argc, char **argv)
{
	int failures;
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");
	const char *syms[] = { "get_parsers", "parse" };
	void *funcs[2] = { NULL };
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
	 * A parsing warning can render the offending value with %pd, which
	 * serializes it, so the serializer plugins have to be loaded before any
	 * parse() call or that warning aborts in the serializer lookup.
	 */
	if (plugin_loaded && serializer_g_init()) {
		error("serializer_g_init() failed");
		plugin_loaded = false;
	}

	if (plugin_loaded) {
		plug = plugin_load_and_link(DATA_PARSER_PLUGIN_NAME,
					    ARRAY_SIZE(syms), syms, funcs);

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
	if (plugin_loaded) {
		((get_parsers_func_t) funcs[0])(&parsers, &parser_count);
		parse_func = (parse_func_t) funcs[1];
	}

	/*
	 * The suite can only be built once parser_count is known as
	 * tcase_add_loop_test() needs it, which is why the plugin is not
	 * loaded from a libcheck fixture.
	 */
	tcase = tcase_create("parsers");
	tcase_add_test(tcase, test_parsers_loaded);
	tcase_add_loop_test(tcase, test_parser, 0, parser_count);
	tcase_add_test(tcase, test_deprecated_field_alone);
	tcase_add_test(tcase, test_deprecated_field_overloaded);

	suite = suite_create(DATA_PARSER_PLUGIN_TYPE);
	suite_add_tcase(suite, tcase);

	/* Create and run the runner */
	sr = srunner_create(suite);
	srunner_run_all(sr, CK_VERBOSE);
	failures = srunner_ntests_failed(sr);
	srunner_free(sr);

	xfree(expected_warn_source);
	xfree(bad_warn_source);

	plugin_unload(plug);
	serializer_g_fini();
	slurm_conf_destroy();
	log_fini();

	return failures;
}
