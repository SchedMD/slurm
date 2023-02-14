/*****************************************************************************\
 *  openapi.c - Slurm data parser openapi specifier
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
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

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "api.h"
#include "openapi.h"
#include "parsers.h"
#include "parsing.h"

#define MAGIC_SPEC_ARGS 0xa891beab
#define SCHEMAS_PATH OPENAPI_PATH_SEP "components" OPENAPI_PATH_SEP "schemas" OPENAPI_PATH_SEP
#define REF_PATH OPENAPI_PATH_REL SCHEMAS_PATH
#define TYPE_PREFIX "DATA_PARSER_"
#define KEY_PREFIX XSTRINGIFY(DATA_VERSION) "_"

typedef struct {
	int magic; /* MAGIC_SPEC_ARGS */
	args_t *args;
	const parser_t *parsers;
	int parser_count;
	data_t *schemas;
	data_t *spec;
} spec_args_t;

static void _add_parser(const parser_t *parser, spec_args_t *sargs);
static void _replace_refs(data_t *data, spec_args_t *sargs);
extern void _set_ref(data_t *obj, const parser_t *parser, spec_args_t *sargs);
static data_t *_resolve_parser_key(const parser_t *parser, data_t *dst);

static char *_get_parser_key(const parser_t *parser)
{
	char *stype;
	char *key = NULL;

	check_parser(parser);
	xassert(!xstrncmp(parser->type_string, TYPE_PREFIX,
			  strlen(TYPE_PREFIX)));

	stype = xstrtolower(xstrdup(parser->type_string + strlen(TYPE_PREFIX)));
	xstrfmtcat(key, "%s%s", KEY_PREFIX, stype);
	xfree(stype);

	return key;
}

static char *_get_parser_path(const parser_t *parser)
{
	char *key = _get_parser_key(parser);
	char *path = NULL;

	xstrfmtcat(path, "%s%s", REF_PATH, key);
	xfree(key);

	return path;
}

extern data_t *set_openapi_props(data_t *obj, openapi_type_format_t format,
				 const char *desc)
{
	data_t *dtype;
	const char *format_str;

	xassert(format > OPENAPI_FORMAT_INVALID);
	xassert(format < OPENAPI_FORMAT_MAX);

	if (data_get_type(obj) == DATA_TYPE_NULL)
		data_set_dict(obj);

	dtype = data_key_set(obj, "type");

	/* type may have already been set by _resolve_parser_key() */
	xassert((data_get_type(dtype) == DATA_TYPE_NULL) ||
		((data_get_type(dtype) == DATA_TYPE_STRING) &&
		 !xstrcmp(data_get_string(dtype), "object")));

	data_set_string(dtype, openapi_type_format_to_type_string(format));

	if ((format_str = openapi_type_format_to_format_string(format))) {
		data_t *dformat = data_key_set(obj, "format");
		xassert(data_get_type(dformat) == DATA_TYPE_NULL);
		data_set_string(dformat, format_str);
	}

	if (desc)
		data_set_string(data_key_set(obj, "description"), desc);

	if (format == OPENAPI_FORMAT_ARRAY)
		return data_set_dict(data_key_set(obj, "items"));

	if (format == OPENAPI_FORMAT_OBJECT)
		return data_set_dict(data_key_set(obj, "properties"));

	return NULL;
}

static bool _should_be_ref(const parser_t *parser)
{
	if ((parser->obj_openapi == OPENAPI_FORMAT_OBJECT) ||
	    (parser->obj_openapi == OPENAPI_FORMAT_ARRAY))
		return true;

	if (parser->array_type || parser->pointer_type || parser->list_type ||
	    parser->fields)
		return true;

	return false;
}

/*
 * Populate OpenAPI specification field using parser
 * IN obj - data_t ptr to specific field in OpenAPI schema
 * IN parser - populate field with info from parser
 *
 * If parser is an ARRAY or OBJECT, the openapi_spec() function will be called
 * from the parser to populate the child fields.
 *
 * RET ptr to "items" for ARRAY or "properties" for OBJECT or NULL
 */
static data_t *_set_openapi_parse(data_t *obj, const parser_t *parser,
				  spec_args_t *sargs)
{
	data_t *props;
	openapi_type_format_t format;

	xassert(parser->magic == MAGIC_PARSER);
	xassert(sargs->magic == MAGIC_SPEC_ARGS);
	xassert(sargs->args->magic == MAGIC_ARGS);
	xassert(parser->model != PARSER_MODEL_ARRAY_SKIP_FIELD);

	/* find all parsers that should be references */
	if (parser->model == PARSER_MODEL_ARRAY_LINKED_FIELD) {
		_set_ref(obj, find_parser_by_type(parser->type), sargs);
		return NULL;
	} else if (parser->pointer_type) {
		_set_ref(obj, find_parser_by_type(parser->pointer_type), sargs);
		return NULL;
	}

	/* parser explicitly overrides the specification */
	if (parser->openapi_spec) {
		parser->openapi_spec(parser, sargs->args, sargs->spec, obj);

		/* the resultant type must match the obj_openapi type */
		xassert(!xstrcmp(data_get_string(data_key_get(obj, "type")),
				 openapi_type_format_to_type_string(
					 parser->obj_openapi)));
		return NULL;
	}

	if (parser->array_type || parser->list_type || parser->flag_bit_array)
		format = OPENAPI_FORMAT_ARRAY;
	else if (parser->fields)
		format = OPENAPI_FORMAT_OBJECT;
	else
		format = parser->obj_openapi;

	xassert(format > OPENAPI_FORMAT_INVALID);
	xassert(format < OPENAPI_FORMAT_MAX);

	if ((props = set_openapi_props(obj, format, parser->obj_desc))) {
		if (parser->array_type) {
			_set_ref(props, find_parser_by_type(parser->array_type),
				 sargs);
		} else if (parser->list_type) {
			_set_ref(props, find_parser_by_type(parser->list_type),
				 sargs);
		} else if (parser->flag_bit_array) {
			data_t *fenums;
			set_openapi_props(props, OPENAPI_FORMAT_STRING,
					  "flags");
			fenums = data_set_list(data_key_set(props, "enum"));

			for (int i = 0; i < parser->flag_bit_array_count; i++)
				data_set_string(data_list_append(fenums),
						parser->flag_bit_array[i].name);
		} else if (parser->fields) {
			data_t *required =
				data_set_list(data_key_set(obj, "required"));

			for (int i = 0; i < parser->field_count; i++) {
				data_t *dchild;
				const parser_t *const pchild =
					&parser->fields[i];

				if (pchild->model ==
				    PARSER_MODEL_ARRAY_SKIP_FIELD)
					continue;

				if (pchild->required) {
					data_set_string(
						data_list_append(required),
						pchild->field_name);
				}

				dchild = _resolve_parser_key(pchild, obj);
				_set_ref(dchild, pchild, sargs);
			}
		} else {
			fatal("%s: parser %s need to provide openapi specification, array type or pointer type",
			      __func__, parser->type_string);
		}
	}

	return props;
}

extern void set_openapi_parse_ref(data_t *obj, const parser_t *parser,
				  data_t *spec, args_t *args)
{
	spec_args_t sargs = {
		.magic = MAGIC_SPEC_ARGS,
		.args = args,
		.spec = spec,
	};

	xassert(parser->magic == MAGIC_PARSER);
	xassert(args->magic == MAGIC_ARGS);

	sargs.schemas = data_resolve_dict_path(spec, SCHEMAS_PATH);

	_set_ref(obj, parser, &sargs);
}

extern void _set_ref(data_t *obj, const parser_t *parser, spec_args_t *sargs)
{
	xassert(sargs->magic == MAGIC_SPEC_ARGS);
	xassert(sargs->args->magic == MAGIC_ARGS);

	if (!_should_be_ref(parser)) {
		_set_openapi_parse(obj, parser, sargs);
		return;
	}

	data_set_string_own(data_key_set(data_set_dict(obj), "$ref"),
			    _get_parser_path(parser));

	_add_parser(parser, sargs);
}

static data_t *_resolve_parser_key(const parser_t *parser, data_t *dst)
{
	int rc;
	data_t *path = data_set_list(data_new());
	data_t *pkey;

	/*
	 * key may be multiple dicts combined.
	 * Need to create each dict needed to complete path.
	 */

	if ((rc = openapi_append_rel_path(path, parser->key)))
		fatal("%s: failed to split %s: %s", __func__, parser->key,
		      slurm_strerror(rc));

	while ((pkey = data_list_dequeue(path))) {
		data_t *props, *type;

		if (data_get_type(dst) == DATA_TYPE_NULL)
			data_set_dict(dst);

		xassert(data_get_type(pkey) == DATA_TYPE_STRING);
		xassert(data_get_type(dst) == DATA_TYPE_DICT);

		if (!(type = data_key_get(dst, "type")))
			data_set_string(data_key_set(dst, "type"), "object");
		else
			xassert(!xstrcmp(data_get_string(
				data_key_get(dst, "type")), "object"));

		props = data_key_set(dst, "properties");

		xassert((data_get_type(props) == DATA_TYPE_DICT) ||
			(data_get_type(props) == DATA_TYPE_NULL));

		if (data_get_type(props) != DATA_TYPE_DICT)
			data_set_dict(props);

		dst = data_set_dict(data_key_set(props, data_get_string(pkey)));

		FREE_NULL_DATA(pkey);
	}

	FREE_NULL_DATA(path);
	return dst;
}

static void _add_parser(const parser_t *parser, spec_args_t *sargs)
{
	data_t *obj;
	char *key;

	xassert(sargs->magic == MAGIC_SPEC_ARGS);
	xassert(sargs->args->magic == MAGIC_ARGS);

	if (!_should_be_ref(parser)) {
	       debug3("%s: skip adding %s as simple type=%s format=%s",
		      __func__, parser->type_string,
		      openapi_type_format_to_type_string(
			      parser->obj_openapi),
		      openapi_type_format_to_format_string(
			      parser->obj_openapi));
	       return;
	}

	key = _get_parser_key(parser);
	obj = data_key_set(sargs->schemas, key);

	if (data_get_type(obj) != DATA_TYPE_NULL) {
		debug3("%s: skip adding duplicate schema %s",
		      __func__, key);
		xfree(key);
		return;
	}
	xfree(key);

	data_set_dict(obj);
	_set_openapi_parse(obj, parser, sargs);
}

static data_for_each_cmd_t _convert_list_entry(data_t *data, void *arg)
{
	spec_args_t *sargs = arg;

	xassert(sargs->magic == MAGIC_SPEC_ARGS);
	xassert(sargs->args->magic == MAGIC_ARGS);

	if ((data_get_type(data) == DATA_TYPE_LIST) ||
	    (data_get_type(data) == DATA_TYPE_DICT))
		_replace_refs(data, sargs);

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _convert_dict_entry(const char *key, data_t *data,
					       void *arg)
{
	spec_args_t *sargs = arg;

	xassert(sargs->magic == MAGIC_SPEC_ARGS);
	xassert(sargs->args->magic == MAGIC_ARGS);

	if (!xstrcmp(key, "$ref") &&
	    (data_get_type(data) == DATA_TYPE_STRING) &&
	    !xstrncmp(data_get_string(data), TYPE_PREFIX,
		      strlen(TYPE_PREFIX))) {
		const parser_t *parser = NULL;

		for (int i = 0; i < sargs->parser_count; i++) {
			if (!xstrcmp(sargs->parsers[i].type_string,
				     data_get_string(data))) {
				parser = &sargs->parsers[i];
				break;
			}
		}

		if (!parser)
			fatal_abort("%s: unknown %s",
				    __func__, data_get_string(data));

		data_set_string_own(data, _get_parser_path(parser));
		_add_parser(parser, sargs);
	}

	if ((data_get_type(data) == DATA_TYPE_LIST) ||
	    (data_get_type(data) == DATA_TYPE_DICT))
		_replace_refs(data, sargs);

	return DATA_FOR_EACH_CONT;
}

/*
 * Find every $ref = DATA_PARSER_* and add correct path
 */
static void _replace_refs(data_t *data, spec_args_t *sargs)
{
	xassert(sargs->magic == MAGIC_SPEC_ARGS);
	xassert(sargs->args->magic == MAGIC_ARGS);
	xassert(sargs->parsers);
	xassert(sargs->parser_count > 0);

	if (!data)
		return;

	if (data_get_type(data) == DATA_TYPE_DICT)
		(void) data_dict_for_each(data, _convert_dict_entry, sargs);
	else if (data_get_type(data) == DATA_TYPE_LIST)
		(void) data_list_for_each(data, _convert_list_entry, sargs);
}

extern int data_parser_p_specify(args_t *args, data_t *spec)
{
	spec_args_t sargs = {
		.magic = MAGIC_SPEC_ARGS,
		.args = args,
		.spec = spec,
	};

	xassert(args->magic == MAGIC_ARGS);

	if (!spec || (data_get_type(spec) != DATA_TYPE_DICT))
		return error("OpenAPI specification invalid");

	sargs.schemas = data_resolve_dict_path(spec, SCHEMAS_PATH);

	if (!sargs.schemas || (data_get_type(sargs.schemas) != DATA_TYPE_DICT))
		return error("%s not found or invalid type", SCHEMAS_PATH);

	get_parsers(&sargs.parsers, &sargs.parser_count);
	_replace_refs(spec, &sargs);

	return SLURM_SUCCESS;
}
