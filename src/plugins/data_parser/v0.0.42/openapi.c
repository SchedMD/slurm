/*****************************************************************************\
 *  openapi.c - Slurm data parser openapi specifier
 *****************************************************************************
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

#include "src/common/data.h"
#include "src/common/http.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "api.h"
#include "openapi.h"
#include "parsers.h"
#include "parsing.h"

#define MAGIC_SPEC_ARGS 0xa891beab
#define REF_PATH OPENAPI_PATH_REL OPENAPI_SCHEMAS_PATH
#define TYPE_PREFIX "DATA_PARSER_"
#define KEY_PREFIX XSTRINGIFY(DATA_VERSION) "_"
#define IS_FLAG_BIT_DEPRECATED(bit) (bit->deprecated)
#define IS_PARSER_DEPRECATED(parser) (parser->deprecated)

typedef struct {
	int magic; /* MAGIC_SPEC_ARGS */
	args_t *args;
	const parser_t *parsers;
	int parser_count;
	data_t *paths; /* existing paths in OAS */
	data_t *new_paths; /* newly populated paths */
	data_t *schemas;
	data_t *spec;
	data_t *path_params; /* dict of each path param */
	data_t *params; /* current parameters target */
	int *references; /* references[i] = count(parsers[i]) */
	bool disable_refs;
} spec_args_t;

#define MAGIC_REFS_PTR 0xaa910e8b

typedef struct {
	int magic; /* MAGIC_REFS_PTR */
	int *references; /* references[i] = count(parsers[i]) */
} refs_ptr_t;

static void _replace_refs(data_t *data, spec_args_t *sargs);
static void _count_refs(data_t *data, spec_args_t *sargs);
extern void _set_ref(data_t *obj, const parser_t *parent,
		     const parser_t *parser, spec_args_t *sargs);
static data_t *_resolve_parser_key(const parser_t *parser, data_t *dst);

static uint32_t _resolve_parser_index(const parser_t *parser,
				      spec_args_t *sargs)
{
	for (int i = 0; i < sargs->parser_count; i++)
		if (parser->type == sargs->parsers[i].type)
			return i;

	xassert(false);
	return NO_VAL;
}

static const parser_t *_resolve_parser(const char *type, spec_args_t *sargs)
{
	for (int i = 0; i < sargs->parser_count; i++)
		if (!xstrcmp(sargs->parsers[i].type_string, type))
			return &sargs->parsers[i];

	return NULL;
}

static char *_get_parser_key(const parser_t *parser)
{
	char *stype;
	char *key = NULL;

	check_parser(parser);
	xassert(!xstrncmp(parser->type_string, TYPE_PREFIX,
			  strlen(TYPE_PREFIX)));

	stype = xstrdup(parser->type_string + strlen(TYPE_PREFIX));
	xstrtolower(stype);
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

/*
 * Populate OpenAPI specification field
 * IN obj - data_t ptr to specific field in OpenAPI schema
 * IN format - OpenAPI format to use
 * IN desc - Description of field to use
 * RET ptr to "items" for ARRAY or "properties" for OBJECT or NULL
 */
static data_t *_set_openapi_props(data_t *obj, openapi_type_format_t format,
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

static bool _should_be_ref(const parser_t *parser, spec_args_t *sargs)
{
	uint32_t parser_index;

	if (sargs->disable_refs)
		return false;

	/*
	 * Removed parsers/fields are just place holders and using $ref will
	 * result in an invalid $ref include path.
	 */
	if (parser->model == PARSER_MODEL_REMOVED)
		return false;
	if (parser->model == PARSER_MODEL_ARRAY_REMOVED_FIELD)
		return false;

	parser_index = _resolve_parser_index(parser, sargs);

	/* parser with single reference doesn't need to be a $ref */
	if ((parser_index != NO_VAL) && !is_prefer_refs_mode(sargs->args)) {
		debug4("%s: %s references=%u",
		       __func__, parser->type_string,
		       sargs->references[parser_index]);

		if (sargs->references[parser_index] <= 1)
			return false;
	}

	if ((parser->obj_openapi == OPENAPI_FORMAT_OBJECT) ||
	    ((parser->obj_openapi == OPENAPI_FORMAT_ARRAY) &&
	     !((is_inline_enums_mode(sargs->args)) &&
	       (parser->model == PARSER_MODEL_FLAG_ARRAY))))
		return true;

	if (parser->array_type || parser->pointer_type || parser->list_type ||
	    parser->fields || parser->alias_type)
		return true;

	return false;
}

static void _add_eflags(data_t *props, const parser_t *parser,
			spec_args_t *sargs)
{
	parser = find_parser_by_type(parser->type);

	for (int i = 0; i < parser->flag_bit_array_count; i++) {
		const flag_bit_t *bit = &parser->flag_bit_array[i];
		data_t *dchild;

		if (bit->hidden)
			continue;

		dchild = data_key_set(props, bit->name);
		_set_openapi_props(dchild, OPENAPI_FORMAT_BOOL, NULL);
	}
}

static void _add_field(data_t *obj, data_t *required,
		       const parser_t *const parent,
		       const parser_t *const pchild, spec_args_t *sargs)
{
	data_t *dchild;

	if (pchild->model == PARSER_MODEL_ARRAY_SKIP_FIELD)
		return;

	if (pchild->required)
		data_set_string(data_list_append(required), pchild->key);

	dchild = _resolve_parser_key(pchild, obj);

	if (pchild->model ==
	    PARSER_MODEL_ARRAY_LINKED_EXPLODED_FLAG_ARRAY_FIELD) {
		data_t *p = data_key_get(dchild, "properties");
		_add_eflags(p, pchild, sargs);
	} else {
		_set_ref(dchild, parent, pchild, sargs);
	}
}

static void _add_param_flag_enum(data_t *param, const parser_t *parser)
{
	data_t *fenums = data_set_list(data_key_set(param, "enum"));
	data_set_string(data_key_set(param, "type"),
		openapi_type_format_to_type_string(OPENAPI_FORMAT_STRING));

	for (int i = 0; i < parser->flag_bit_array_count; i++)
		if (!parser->flag_bit_array[i].hidden)
			data_set_string(data_list_append(fenums),
					parser->flag_bit_array[i].name);
}

/*
 * Populate OpenAPI specification field using parser
 * IN obj - data_t ptr to specific field in OpenAPI schema
 * IN parser - populate field with info from parser
 * IN description - description from parent pointer parser or NULL
 * IN deprecated - parser marked as deprecated
 *
 * RET ptr to "items" for ARRAY or "properties" for OBJECT or NULL
 */
static data_t *_set_openapi_parse(data_t *obj, const parser_t *parser,
				  spec_args_t *sargs, const char *desc,
				  bool deprecated)
{
	data_t *props;
	openapi_type_format_t format;

	xassert(parser->magic == MAGIC_PARSER);
	xassert(sargs->magic == MAGIC_SPEC_ARGS);
	xassert(sargs->args->magic == MAGIC_ARGS);
	xassert(parser->model != PARSER_MODEL_ARRAY_SKIP_FIELD);
	xassert(!parser->pointer_type);
	xassert(!parser->alias_type);
	xassert(parser->model != PARSER_MODEL_ARRAY_LINKED_FIELD);
	xassert(parser->model !=
		PARSER_MODEL_ARRAY_LINKED_EXPLODED_FLAG_ARRAY_FIELD);
	xassert(parser->model != PARSER_MODEL_ARRAY_REMOVED_FIELD);

	if (parser->array_type || parser->list_type ||
	    (parser->flag_bit_array && !parser->single_flag))
		format = OPENAPI_FORMAT_ARRAY;
	else if (parser->flag_bit_array && parser->single_flag)
		format = OPENAPI_FORMAT_STRING;
	else if (parser->fields)
		format = OPENAPI_FORMAT_OBJECT;
	else
		format = parser->obj_openapi;

	xassert(format > OPENAPI_FORMAT_INVALID);
	xassert(format < OPENAPI_FORMAT_MAX);

	if (parser->obj_desc && !desc)
		desc = parser->obj_desc;

	if ((props = _set_openapi_props(obj, format, desc))) {
		if (parser->array_type) {
			_set_ref(props, parser,
				 find_parser_by_type(parser->array_type),
				 sargs);
		} else if (parser->list_type) {
			_set_ref(props, parser,
				 find_parser_by_type(parser->list_type), sargs);
		} else if (parser->flag_bit_array) {
			_add_param_flag_enum(props, parser);
		} else if (parser->fields) {
			data_t *required =
				data_set_list(data_key_set(obj, "required"));

			for (int i = 0; i < parser->field_count; i++)
				_add_field(obj, required, parser,
					   &parser->fields[i], sargs);
		} else if (parser->model == PARSER_MODEL_REMOVED) {
			/* do nothing */
		} else if (!is_complex_mode(sargs->args)) {
			fatal("%s: parser %s need to provide openapi specification, array type or pointer type",
			      __func__, parser->type_string);
		}
	}

	if (deprecated)
		data_set_bool(data_key_set(obj, "deprecated"), true);

	return props;
}

extern void _set_ref(data_t *obj, const parser_t *parent,
		     const parser_t *parser, spec_args_t *sargs)
{
	char *str, *key;
	const char *desc = NULL;
	bool deprecated = (parent && parent->deprecated);

	xassert(sargs->magic == MAGIC_SPEC_ARGS);
	xassert(sargs->args->magic == MAGIC_ARGS);

	while (true) {
		if (desc)
			/* do nothing */;
		else if (parent && parent->obj_desc)
			desc = parent->obj_desc;
		else if (parser->obj_desc)
			desc = parser->obj_desc;

		/* All children are deprecated once the parent is */
		if (parser->deprecated)
			deprecated = true;

		if (parser->model == PARSER_MODEL_REMOVED) {
			if (is_complex_mode(sargs->args))
				return;
			break;
		}

		if ((parser->model == PARSER_MODEL_ARRAY_LINKED_FIELD) ||
		    (parser->model ==
		     PARSER_MODEL_ARRAY_LINKED_EXPLODED_FLAG_ARRAY_FIELD) ||
		    (parser->model == PARSER_MODEL_ARRAY_REMOVED_FIELD)) {
			/* resolve to linked parser */
			parent = parser;
			parser = find_parser_by_type(parser->type);

			continue;
		}

		if (parser->pointer_type) {
			parser = find_parser_by_type(parser->pointer_type);
			continue;
		}

		if (parser->alias_type) {
			parser = find_parser_by_type(parser->alias_type);
			continue;
		}

		break;
	}

	if (!_should_be_ref(parser, sargs)) {
		_set_openapi_parse(obj, parser, sargs, desc, deprecated);
		return;
	}

	if (data_get_type(obj) == DATA_TYPE_NULL)
		data_set_dict(obj);

	xassert(data_get_type(obj) == DATA_TYPE_DICT);

	str = _get_parser_path(parser);
	data_set_string_own(data_key_set(obj, "$ref"), str);

	if (desc && !data_key_get(obj, "description"))
		data_set_string(data_key_set(obj, "description"), desc);

	if (deprecated)
		data_set_bool(data_key_set(obj, "deprecated"), true);

	/* Add schema for $ref target */

	key = _get_parser_key(parser);
	obj = data_key_set(sargs->schemas, key);

	if (data_get_type(obj) == DATA_TYPE_NULL) {
		debug4("%s: adding schema %s", __func__, key);
		_set_openapi_parse(data_set_dict(obj), parser, sargs, NULL,
				   parser->deprecated);
	} else {
		debug4("%s: skip adding duplicate schema %s", __func__, key);
	}

	xfree(key);
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

		dst = data_key_set(props, data_get_string(pkey));

		if (data_get_type(dst) == DATA_TYPE_NULL)
			data_set_dict(dst);

		xassert(data_get_type(dst) == DATA_TYPE_DICT);

		FREE_NULL_DATA(pkey);
	}

	FREE_NULL_DATA(path);
	return dst;
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
	data_t *ref;

	xassert(sargs->magic == MAGIC_SPEC_ARGS);
	xassert(sargs->args->magic == MAGIC_ARGS);
	xassert(sargs->parsers);
	xassert(sargs->parser_count > 0);

	if (!data)
		return;

	if (data_get_type(data) == DATA_TYPE_LIST)
		(void) data_list_for_each(data, _convert_list_entry, sargs);

	if (data_get_type(data) != DATA_TYPE_DICT)
		return;

	if ((ref = data_key_get(data, "$ref")) &&
	     (data_get_type(ref) == DATA_TYPE_STRING) &&
	     !xstrncmp(data_get_string(ref), TYPE_PREFIX,
		       strlen(TYPE_PREFIX))) {
		const parser_t *parser = NULL;

		for (int i = 0; i < sargs->parser_count; i++) {
			if (!xstrcmp(sargs->parsers[i].type_string,
				     data_get_string(ref))) {
				parser = &sargs->parsers[i];
				break;
			}
		}

		if (!parser) {
			debug("%s: skipping unknown %s",
			      __func__, data_get_string(data));
			data_set_null(data);
			return;
		}

		_set_ref(data, NULL, parser, sargs);
	} else {
		(void) data_dict_for_each(data, _convert_dict_entry, sargs);
	}
}

static data_for_each_cmd_t _count_list_entry(data_t *data, void *arg)
{
	spec_args_t *sargs = arg;

	xassert(sargs->magic == MAGIC_SPEC_ARGS);
	xassert(sargs->args->magic == MAGIC_ARGS);

	if ((data_get_type(data) == DATA_TYPE_LIST) ||
	    (data_get_type(data) == DATA_TYPE_DICT))
		_count_refs(data, sargs);

	return DATA_FOR_EACH_CONT;
}

static void _increment_ref(const parser_t *parent, const parser_t *parser,
			   spec_args_t *sargs)
{
	uint32_t parser_index;

	parser = unalias_parser(parser);

	if ((parser_index = _resolve_parser_index(parser, sargs)) != NO_VAL) {
		sargs->references[parser_index]++;

		debug4("%s: %s->%s incremented references=%u",
		       __func__, (parent ? parent->type_string : "*" ),
		       parser->type_string, sargs->references[parser_index]);
	}
}

static data_for_each_cmd_t _count_dict_entry(const char *key, data_t *data,
					     void *arg)
{
	spec_args_t *sargs = arg;

	xassert(sargs->magic == MAGIC_SPEC_ARGS);
	xassert(sargs->args->magic == MAGIC_ARGS);

	if (!xstrcmp(key, "$ref") &&
	    (data_get_type(data) == DATA_TYPE_STRING) &&
	    !xstrncmp(data_get_string(data), TYPE_PREFIX,
		      strlen(TYPE_PREFIX)))
		_increment_ref(NULL, _resolve_parser(data_get_string(data),
						     sargs), sargs);

	if ((data_get_type(data) == DATA_TYPE_LIST) ||
	    (data_get_type(data) == DATA_TYPE_DICT))
		_count_refs(data, sargs);

	return DATA_FOR_EACH_CONT;
}

/*
 * Find every $ref = DATA_PARSER_* and count references
 */
static void _count_refs(data_t *data, spec_args_t *sargs)
{
	xassert(sargs->magic == MAGIC_SPEC_ARGS);
	xassert(sargs->args->magic == MAGIC_ARGS);
	xassert(sargs->parsers);
	xassert(sargs->parser_count > 0);

	if (!data)
		return;

	if (data_get_type(data) == DATA_TYPE_DICT)
		(void) data_dict_for_each(data, _count_dict_entry, sargs);
	else if (data_get_type(data) == DATA_TYPE_LIST)
		(void) data_list_for_each(data, _count_list_entry, sargs);
}

static void _count_parser_refs(spec_args_t *sargs)
{
	xassert(sargs->magic == MAGIC_SPEC_ARGS);
	xassert(sargs->args->magic == MAGIC_ARGS);
	xassert(sargs->parsers);
	xassert(sargs->parser_count > 0);

	for (int ip = 0; ip < sargs->parser_count; ip++) {
		const parser_t *parser = &sargs->parsers[ip];

		if ((parser->model != PARSER_MODEL_ARRAY) ||
		    !parser->field_count)
			continue;

		for (int i = 0; i < parser->field_count; i++) {
			const parser_t *pchild =
				find_parser_by_type(parser->fields[i].type);

			if (pchild)
				_increment_ref(parser, pchild, sargs);
		}
	}
}

static data_t *_add_param(data_t *param, const char *name,
			  openapi_type_format_t format, bool allow_empty,
			  const char *desc, bool deprecated, bool required,
			  spec_args_t *args)
{
	data_t *schema;
	bool in_path = data_key_get(args->path_params, name);

	xassert(format > OPENAPI_FORMAT_INVALID);
	xassert(format < OPENAPI_FORMAT_MAX);

	data_set_string(data_key_set(param, "in"),
			(in_path ? "path" : "query"));
	xassert(name);
	data_set_string(data_key_set(param, "name"), name);
	data_set_string(data_key_set(param, "style"),
			(in_path ? "simple" : "form"));
	data_set_bool(data_key_set(param, "explode"), false);
	if (deprecated)
		data_set_bool(data_key_set(param, "deprecated"), true);
	data_set_bool(data_key_set(param, "allowEmptyValue"), allow_empty);
	data_set_bool(data_key_set(param, "allowReserved"), false);
	if (desc)
		data_set_string(data_key_set(param, "description"), desc);
	data_set_bool(data_key_set(param, "required"), (in_path || required));

	schema = data_set_dict(data_key_set(param, "schema"));
	data_set_string(data_key_set(schema, "type"), "string");

	return schema;
}

static void _add_param_eflags(data_t *params, const parser_t *parser,
			      spec_args_t *args)
{
	parser = find_parser_by_type(parser->type);

	for (int i = 0; i < parser->flag_bit_array_count; i++) {
		const flag_bit_t *bit = &parser->flag_bit_array[i];

		if (!bit->hidden)
			_add_param(data_set_dict(data_list_append(params)),
				   bit->name, OPENAPI_FORMAT_BOOL, true,
				   bit->description,
				   IS_FLAG_BIT_DEPRECATED(bit), false, args);
	}
}

static void _add_param_linked(data_t *params, const parser_t *fp,
			      spec_args_t *args)
{
	data_t *schema;
	const parser_t *p;

	if (fp->model == PARSER_MODEL_ARRAY_SKIP_FIELD) {
		return;
	} else if (fp->model ==
		   PARSER_MODEL_ARRAY_LINKED_EXPLODED_FLAG_ARRAY_FIELD) {
		_add_param_eflags(params, fp, args);
		return;
	} else if (fp->model == PARSER_MODEL_ARRAY_LINKED_FIELD) {
		p = find_parser_by_type(fp->type);
	} else {
		p = fp;
	}

	/* resolve out pointer type to first non-pointer */
	p = unalias_parser(p);

	if (p->model == PARSER_MODEL_ARRAY) {
		/* no way to parse an dictionary/object currently */
		return;
	}

	schema = _add_param(data_set_dict(data_list_append(params)), fp->key,
			    OPENAPI_FORMAT_STRING,
			    (p->obj_openapi == OPENAPI_FORMAT_BOOL),
			    fp->obj_desc, IS_PARSER_DEPRECATED(fp),
			    fp->required, args);

	if (fp->model == PARSER_MODEL_ARRAY_LINKED_FIELD)
		fp = find_parser_by_type(fp->type);

	if (fp->flag_bit_array)
		_add_param_flag_enum(schema, fp);
}

static data_for_each_cmd_t _foreach_path_method_ref(data_t *ref, void *arg)
{
	spec_args_t *args = arg;
	const parser_t *parser;

	if (!(parser = _resolve_parser(data_get_string(ref), args))) {
		error("%s: Unable to find parser for $ref = %s",
		      __func__, data_get_string(ref));
		return DATA_FOR_EACH_FAIL;
	}

	/* auto-dereference pointers to avoid unneeded resolution failures */
	parser = unalias_parser(parser);

	if (parser->model != PARSER_MODEL_ARRAY) {
		error("$ref parameters must be an array parser");
		return DATA_FOR_EACH_FAIL;
	}

	debug3("$ref=%s found parser %s(0x%"PRIxPTR")=%s",
	       data_get_string(ref), parser->type_string, (uintptr_t) parser,
	       parser->obj_type_string);

	for (int i = 0; i < parser->field_count; i++)
		_add_param_linked(args->params, &parser->fields[i], args);

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _foreach_path_method(const char *key, data_t *data,
						void *arg)
{
	spec_args_t *args = arg;
	data_t *params, *ref, *refs;
	int rc = DATA_FOR_EACH_CONT;

	if (data_get_type(data) != DATA_TYPE_DICT)
		return DATA_FOR_EACH_CONT;

	if (!(params = data_key_get(data, OPENAPI_PATH_PARAMS_FIELD)))
		return DATA_FOR_EACH_CONT;

	if (data_get_type(params) != DATA_TYPE_DICT)
		return DATA_FOR_EACH_CONT;

	if (!(ref = data_key_get(params, OPENAPI_REF_TAG)))
		return DATA_FOR_EACH_CONT;

	refs = data_new();
	data_move(refs, ref);
	args->params = data_set_list(params);

	if (data_get_type(refs) == DATA_TYPE_LIST) {
		if (data_list_for_each(refs, _foreach_path_method_ref,
				       args) < 0)
			rc = DATA_FOR_EACH_FAIL;
	} else if (data_get_type(refs) == DATA_TYPE_STRING) {
		rc = _foreach_path_method_ref(refs, args);
	} else {
		error("$ref must be string or dict");
		return DATA_FOR_EACH_FAIL;
	}

	FREE_NULL_DATA(refs);
	return rc;
}

static data_for_each_cmd_t _foreach_path_entry(data_t *data, void *arg)
{
	spec_args_t *args = arg;
	char *path, *path2;

	if (data_convert_type(data, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		return DATA_FOR_EACH_FAIL;

	path = xstrdup(data_get_string(data));

	if (path[0] != '{') {
		xfree(path);
		return DATA_FOR_EACH_CONT;
	}

	if ((path2 = xstrstr(path, "}")))
		*path2 = '\0';

	data_key_set(args->path_params, (path + 1));

	xfree(path);

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _foreach_path(const char *key, data_t *data,
					 void *arg)
{
	int rc = SLURM_SUCCESS;
	char *param, *start, *end, *replaced;
	spec_args_t *args = arg;
	data_t *n, *path;

	param = xstrdup(key);

	if (!(start = xstrstr(param, OPENAPI_DATA_PARSER_PARAM))) {
		xfree(param);
		return DATA_FOR_EACH_CONT;
	}

	*start = '\0';
	end = start + strlen(OPENAPI_DATA_PARSER_PARAM);
	replaced = xstrdup_printf("%s%s%s", param, XSTRINGIFY(DATA_VERSION),
				  end);
	xfree(param);

	if (!args->new_paths)
		args->new_paths = data_set_dict(data_new());

	n = data_key_set(args->new_paths, replaced);

	data_copy(n, data);

	args->path_params = data_set_dict(data_new());
	path = parse_url_path(replaced, false, true);
	if (data_list_for_each(path, _foreach_path_entry, args) < 0)
		rc = SLURM_ERROR;
	FREE_NULL_DATA(path);

	if (!rc && (data_dict_for_each(n, _foreach_path_method, args) < 0))
		rc = SLURM_ERROR;

	xfree(replaced);
	FREE_NULL_DATA(args->path_params);

	return rc ? DATA_FOR_EACH_FAIL : DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _foreach_join_path(const char *key, data_t *data,
					      void *arg)
{
	spec_args_t *args = arg;
	data_t *path = data_key_set(args->paths, key);

	data_move(path, data);
	_count_refs(path, args);
	_count_parser_refs(args);
	_replace_refs(path, args);

	return DATA_FOR_EACH_CONT;
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

	sargs.schemas = data_resolve_dict_path(spec, OPENAPI_SCHEMAS_PATH);
	sargs.paths = data_resolve_dict_path(spec, OPENAPI_PATHS_PATH);

	if (!sargs.schemas || (data_get_type(sargs.schemas) != DATA_TYPE_DICT))
		return error("%s not found or invalid type",
			     OPENAPI_SCHEMAS_PATH);

	get_parsers(&sargs.parsers, &sargs.parser_count);
	sargs.references = xcalloc(sargs.parser_count,
				   sizeof(*sargs.references));

	(void) data_dict_for_each(sargs.paths, _foreach_path, &sargs);
	(void) data_dict_for_each(sargs.new_paths, _foreach_join_path, &sargs);
	FREE_NULL_DATA(sargs.new_paths);
	xfree(sargs.references);

	return SLURM_SUCCESS;
}

extern void set_openapi_schema(data_t *dst, const parser_t *parser,
			       args_t *args)
{
	spec_args_t sargs = {
		.magic = MAGIC_SPEC_ARGS,
		.args = args,
		.spec = dst,
		.disable_refs = true,
	};

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_dict(dst);

	get_parsers(&sargs.parsers, &sargs.parser_count);

	(void) _set_openapi_parse(dst, parser, &sargs, NULL, false);
}

extern int data_parser_p_increment_reference(args_t *args,
					     data_parser_type_t type,
					     refs_ptr_t **references_ptr)
{
	spec_args_t sargs = {
		.magic = MAGIC_SPEC_ARGS,
		.args = args,
	};
	refs_ptr_t *refs = *references_ptr;
	const parser_t *parser;

	get_parsers(&sargs.parsers, &sargs.parser_count);

	if (!refs) {
		refs = *references_ptr = xmalloc(sizeof(*refs));
		refs->magic = MAGIC_REFS_PTR;
		refs->references =
			xcalloc(sargs.parser_count, sizeof(*refs->references));
	}

	if (!(parser = find_parser_by_type(type)))
		return ESLURM_DATA_INVALID_PARSER;

	xassert(refs->magic == MAGIC_REFS_PTR);
	xassert(args->magic == MAGIC_ARGS);
	xassert(type > DATA_PARSER_TYPE_INVALID);
	xassert(type < DATA_PARSER_TYPE_MAX);
	xassert(sargs.parser_count > 0);

	sargs.references = refs->references,
	_increment_ref(NULL, parser, &sargs);

	return SLURM_SUCCESS;
}

extern int data_parser_p_populate_schema(args_t *args, data_parser_type_t type,
					 refs_ptr_t **references_ptr,
					 data_t *dst, data_t *schemas)
{
#ifndef NDEBUG
	refs_ptr_t *refs = *references_ptr;
#endif
	spec_args_t sargs = {
		.magic = MAGIC_SPEC_ARGS,
		.args = args,
		.schemas = schemas,
		.references = (*references_ptr)->references,
	};
	const parser_t *parser;

	xassert(refs && refs->magic == MAGIC_REFS_PTR);
	xassert(args->magic == MAGIC_ARGS);
	xassert(type > DATA_PARSER_TYPE_INVALID);
	xassert(type < DATA_PARSER_TYPE_MAX);
	xassert(data_get_type(dst) == DATA_TYPE_DICT);

	get_parsers(&sargs.parsers, &sargs.parser_count);
	if (!(parser = find_parser_by_type(type)))
		return ESLURM_DATA_INVALID_PARSER;

	_set_ref(dst, NULL, parser, &sargs);

	return SLURM_SUCCESS;
}

extern int data_parser_p_populate_parameters(args_t *args,
					     data_parser_type_t parameter_type,
					     data_parser_type_t query_type,
					     refs_ptr_t **references_ptr,
					     data_t *dst, data_t *schemas)
{
#ifndef NDEBUG
	refs_ptr_t *refs = *references_ptr;
#endif
	spec_args_t sargs = {
		.magic = MAGIC_SPEC_ARGS,
		.args = args,
		.schemas = schemas,
		.references = (*references_ptr)->references,
	};
	const parser_t *param_parser = NULL, *query_parser = NULL;

	xassert(refs && refs->magic == MAGIC_REFS_PTR);
	xassert(args->magic == MAGIC_ARGS);
	xassert(!parameter_type || (parameter_type > DATA_PARSER_TYPE_INVALID));
	xassert(!parameter_type || (parameter_type < DATA_PARSER_TYPE_MAX));
	xassert(!query_type || (query_type > DATA_PARSER_TYPE_INVALID));
	xassert(!query_type || (query_type < DATA_PARSER_TYPE_MAX));
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	get_parsers(&sargs.parsers, &sargs.parser_count);

	sargs.path_params = data_set_dict(data_new());

	if (parameter_type &&
	    !(param_parser =
		      unalias_parser(find_parser_by_type(parameter_type))))
		return ESLURM_DATA_INVALID_PARSER;
	if (query_type &&
	    !(query_parser = unalias_parser(find_parser_by_type(query_type))))
		return ESLURM_DATA_INVALID_PARSER;

	if (param_parser) {
		if (param_parser->model != PARSER_MODEL_ARRAY)
			fatal_abort("parameters must be an array parser");

		debug3("%s: adding parameter %s(0x%"PRIxPTR")=%s to %pd",
		       __func__, param_parser->type_string,
		       (uintptr_t) param_parser, param_parser->obj_type_string,
		       dst);

		for (int i = 0; i < param_parser->field_count; i++)
			data_key_set(sargs.path_params,
				     param_parser->fields[i].key);

		for (int i = 0; i < param_parser->field_count; i++)
			_add_param_linked(dst, &param_parser->fields[i],
					  &sargs);
	}
	if (query_parser) {
		if (query_parser->model != PARSER_MODEL_ARRAY)
			fatal_abort("parameters must be an array parser");

		debug3("%s: adding parameter %s(0x%"PRIxPTR")=%s to %pd",
		       __func__, query_parser->type_string,
		       (uintptr_t) query_parser, query_parser->obj_type_string,
		       dst);

		for (int i = 0; i < query_parser->field_count; i++)
			_add_param_linked(dst, &query_parser->fields[i],
					  &sargs);
	}

	FREE_NULL_DATA(sargs.path_params);

	return SLURM_SUCCESS;
}

extern void data_parser_p_release_references(args_t *args,
					     refs_ptr_t **references_ptr)
{
	refs_ptr_t *refs = *references_ptr;

	xassert(args->magic == MAGIC_ARGS);

	if (!refs)
		return;

	*references_ptr = NULL;

	xassert(refs->magic == MAGIC_REFS_PTR);
	xfree(refs->references);
	refs->magic = ~MAGIC_REFS_PTR;
	xfree(refs);
}
