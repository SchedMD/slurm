/*****************************************************************************\
 *  parsers.c - Slurm data parsers
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
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "alloc.h"
#include "api.h"
#include "events.h"
#include "openapi.h"
#include "parsers.h"
#include "parsing.h"
#include "slurmdb_helpers.h"

#define MAGIC_FOREACH_LIST_FLAG 0xa1d4acd2
#define MAGIC_FOREACH_LIST 0xaefa2af3
#define MAGIC_FOREACH_NT_ARRAY 0xaba1be2b
#define MAGIC_FOREACH_PARSE_MARRAY 0xa081be2b

typedef struct {
	int magic;
	ssize_t index;
	args_t *args;
	const parser_t *const parser;
	list_t *list;
	data_t *dlist;
	data_t *parent_path;
} foreach_list_t;

typedef struct {
	int magic; /* MAGIC_FOREACH_LIST_FLAG */
	args_t *args;
	const parser_t *const parser;
	void *dst; /* already has offset applied */
	data_t *parent_path;
	ssize_t index;
	uint64_t set;
} foreach_flag_parser_args_t;

typedef struct {
	int magic; /* MAGIC_FOREACH_NT_ARRAY */
	void **array; /* array of pointers to objects */
	void *sarray; /* array of objects */
	int index;
	const parser_t *const parser;
	const parser_t *const array_parser;
	args_t *args;
	data_t *parent_path;
} foreach_nt_array_t;

typedef struct {
	int magic; /* MAGIC_FOREACH_PARSE_MARRAY */
	args_t *args;
	const parser_t *const array;
	data_t *parent_path;
	data_t *path;
} parse_marray_args_t;

static void _set_flag_bit(const parser_t *const parser, void *dst,
			  const flag_bit_t *bit, bool matched, const char *path,
			  data_t *src)
{
	/* C allows compiler to choose a size for the enum */
	if (parser->size == sizeof(uint64_t)) {
		uint64_t *flags = dst;
		if (matched)
			*flags |= bit->mask & bit->value;
		else
			*flags &= ~bit->mask | (bit->mask & ~bit->value);
	} else if (parser->size == sizeof(uint32_t)) {
		uint32_t *flags = dst;
		if (matched)
			*flags |= bit->mask & bit->value;
		else
			*flags &= ~bit->mask | (bit->mask & ~bit->value);
	} else if (parser->size == sizeof(uint16_t)) {
		uint16_t *flags = dst;
		if (matched)
			*flags |= bit->mask & bit->value;
		else
			*flags &= ~bit->mask | (bit->mask & ~bit->value);
	} else if (parser->size == sizeof(uint8_t)) {
		uint8_t *flags = dst;
		if (matched)
			*flags |= bit->mask & bit->value;
		else
			*flags &= ~bit->mask | (bit->mask & ~bit->value);
	} else {
		fatal_abort("%s: unexpected enum size: %zu",
			    __func__, parser->size);
	}
}

static void _set_flag_bit_equal(const parser_t *const parser, void *dst,
				const flag_bit_t *bit, bool matched,
				const char *path, data_t *src)
{
	/* C allows compiler to choose a size for the enum
	 *
	 * If the comparsion is all or nothing, then clear all the masked bits
	 * if it doesnt match which means parser order matters with these.
	 */
	if (parser->size == sizeof(uint64_t)) {
		uint64_t *flags = dst;
		if (matched)
			*flags = (*flags & ~bit->mask) |
				 (bit->mask & bit->value);
		else
			*flags &= ~bit->mask;
	} else if (parser->size == sizeof(uint32_t)) {
		uint32_t *flags = dst;
		if (matched)
			*flags = (*flags & ~bit->mask) |
				 (bit->mask & bit->value);
		else
			*flags &= ~bit->mask;
	} else if (parser->size == sizeof(uint16_t)) {
		uint16_t *flags = dst;
		if (matched)
			*flags = (*flags & ~bit->mask) |
				 (bit->mask & bit->value);
		else
			*flags &= ~bit->mask;
	} else if (parser->size == sizeof(uint8_t)) {
		uint8_t *flags = dst;
		if (matched)
			*flags = (*flags & ~bit->mask) |
				 (bit->mask & bit->value);
		else
			*flags &= ~bit->mask;
	} else {
		fatal_abort("%s: unexpected enum size: %zu",
			    __func__, parser->size);
	}
}

static char *_flag_parent_path(char **path_ptr,
			       foreach_flag_parser_args_t *args)
{
	data_t *ppath;

	if (*path_ptr)
		return *path_ptr;

	if (is_fast_mode(args->args))
		return NULL;

	ppath = clone_source_path_index(args->parent_path, args->index);
	set_source_path(path_ptr, args->args, ppath);
	FREE_NULL_DATA(ppath);

	return *path_ptr;
}

static data_for_each_cmd_t _foreach_flag_parser(data_t *src, void *arg)
{
	foreach_flag_parser_args_t *args = arg;
	void *dst = args->dst;
	const parser_t *const parser = args->parser;
	char *path = NULL;
	bool matched_any = false;
	data_for_each_cmd_t rc = DATA_FOR_EACH_CONT;

	xassert(args->magic == MAGIC_FOREACH_LIST_FLAG);
	xassert(args->args->magic == MAGIC_ARGS);
	xassert(parser->magic == MAGIC_PARSER);

	path = _flag_parent_path(&path, args);

	if (data_convert_type(src, DATA_TYPE_STRING) != DATA_TYPE_STRING) {
		on_warn(PARSING, parser->type, args->args, path,
			__func__, "Ignoring unexpected field of type %s",
			data_get_type_string(src));
		goto cleanup;
	}

	for (int8_t i = 0; (i < parser->flag_bit_array_count); i++) {
		const flag_bit_t *bit = &parser->flag_bit_array[i];
		bool matched = !xstrcasecmp(data_get_string(src), bit->name);

		if (matched)
			matched_any = true;

		if (bit->type == FLAG_BIT_TYPE_BIT) {
			uint64_t value = (bit->mask & bit->value);

			if (matched || (~args->set & value) == value)
				_set_flag_bit(parser, dst, bit, matched, path, src);
			args->set |= value;
		} else if (bit->type == FLAG_BIT_TYPE_EQUAL) {
			if (matched || ((~args->set & bit->mask) == bit->mask))
				_set_flag_bit_equal(parser, dst, bit, matched,
						    path, src);
			args->set |= bit->mask;
		} else if (bit->type == FLAG_BIT_TYPE_REMOVED) {
			if (matched)
				on_warn(PARSING, parser->type, args->args, path,
					__func__,
					"Ignoring deprecated flag: %s",
					bit->name);
		} else
			fatal_abort("%s: invalid bit_flag_t", __func__);

	}

	if (!matched_any)
		on_error(PARSING, parser->type, args->args,
			 ESLURM_DATA_FLAGS_INVALID, path, __func__,
			 "Unknown flag \"%s\"", data_get_string(src));

cleanup:
	xfree(path);
	args->index++;
	return rc;
}

static int _parse_flag(void *dst, const parser_t *const parser, data_t *src,
		       args_t *args, data_t *parent_path)
{
	int rc = SLURM_SUCCESS;
	char *path = NULL;
	foreach_flag_parser_args_t fargs = {
		.magic = MAGIC_FOREACH_LIST_FLAG,
		.args = args,
		.parser = parser,
		.dst = dst,
		.parent_path = parent_path,
	};

	xassert(args->magic == MAGIC_ARGS);
	xassert(parser->magic == MAGIC_PARSER);
	xassert(parser->ptr_offset == NO_VAL);
	xassert(parser->model == PARSER_MODEL_FLAG_ARRAY);

	if (data_get_type(src) == DATA_TYPE_STRING) {
		/* List item may just be a single flag */
		if (_foreach_flag_parser(src, &fargs) != DATA_FOR_EACH_CONT) {
			rc = on_error(PARSING, parser->type, args,
				      ESLURM_DATA_FLAGS_INVALID,
				      set_source_path(&path, args, parent_path),
				      __func__,
				      "Parsing single flag \"%s\" failed",
				      data_get_string(src));

			goto cleanup;
		}
	} else if (data_get_type(src) != DATA_TYPE_LIST) {
		rc = on_error(PARSING, parser->type, args,
			      ESLURM_DATA_FLAGS_INVALID_TYPE,
			      set_source_path(&path, args, parent_path),
			      __func__, "Expected a List but found a %s",
			      data_get_type_string(src));

		goto cleanup;
	/*
	 * Flags need special handling as they are always a LIST with a
	 * matching string value. This requires that each possible flag
	 * must be searched for in the list to know if it is there or
	 * not.
	 */
	} else if (data_list_for_each(src, _foreach_flag_parser, &fargs) < 0) {
		rc = on_error(PARSING, parser->type, args,
			      ESLURM_DATA_FLAGS_INVALID,
			      set_source_path(&path, args, parent_path),
			      __func__, "Parsing flags failed");
		goto cleanup;
	}

cleanup:
	xfree(path);
	return rc;
}

static data_for_each_cmd_t _foreach_parse_list(data_t *src, void *arg)
{
	int rc;
	foreach_list_t *args = arg;
	const parser_t *const parser = args->parser;
	const parser_t *const lparser = find_parser_by_type(parser->list_type);
	void *obj = NULL;
	data_t *ppath = NULL;

	xassert(args->magic == MAGIC_FOREACH_LIST);
	check_parser(parser);
	check_parser(lparser);
	xassert(!args->dlist); /* only for dumping */
	xassert((args->index > 0) || (args->index == -1));

	if (args->index < 0)
		args->index = 0;

	if (!is_fast_mode(args->args)) {
		data_t *ppath_last;

		ppath = data_copy(NULL, args->parent_path);
		ppath_last = data_get_list_last(ppath);

		/* Use jq style array zero based array notation */
		data_set_string_fmt(ppath_last, "%s[%zu]",
				    data_get_string(ppath_last), args->index);
	}

	if ((rc = parse(&obj, NO_VAL, lparser, src, args->args, ppath))) {
		FREE_NULL_DATA(ppath);
		return DATA_FOR_EACH_FAIL;
	}

	args->index++;
	list_append(args->list, obj);

	FREE_NULL_DATA(ppath);
	return DATA_FOR_EACH_CONT;
}

static int _parse_list(const parser_t *const parser, void *dst, data_t *src,
		       args_t *args, data_t *parent_path)
{
	int rc = SLURM_SUCCESS;
	char *path = NULL;
	list_t **list = dst;
	foreach_list_t list_args = {
		.magic = MAGIC_FOREACH_LIST,
		.dlist = NULL,
		.list = *list,
		.args = args,
		.parser = parser,
		.parent_path = parent_path,
		.index = -1,
	};

	xassert(!*list || (list_count(*list) >= 0));
	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	log_flag(DATA, "%s: BEGIN: list parsing %s{%s(0x%"PRIxPTR")} to List 0x%"PRIxPTR" via parser %s(0x%"PRIxPTR")",
		__func__, set_source_path(&path, args, parent_path),
		data_get_type_string(src), (uintptr_t) src,
		(uintptr_t) dst, parser->type_string, (uintptr_t) parser
	);

	if (!list_args.list) {
		const parser_t *const lparser =
			find_parser_by_type(parser->list_type);
		list_args.list = list_create((ListDelF) lparser->free);
	}

	xassert(list_count(list_args.list) >= 0);

	if (data_get_type(src) == DATA_TYPE_LIST) {
		if (data_list_for_each(src, _foreach_parse_list,
				       &list_args) < 0)
			rc = ESLURM_REST_FAIL_PARSING;
	} else if (data_convert_type(src, DATA_TYPE_STRING) ==
		   DATA_TYPE_STRING) {
		/* Assume List is just a single entry */
		if (_foreach_parse_list(src, &list_args) != DATA_FOR_EACH_CONT)
			rc = ESLURM_REST_FAIL_PARSING;
	} else {
		rc = on_error(PARSING, parser->type, args,
			      ESLURM_DATA_EXPECTED_LIST,
			      set_source_path(&path, args, parent_path),
			      __func__, "Expected List but found a %s",
			      data_get_type_string(src));
	}

	if (!rc) {
		*list = list_args.list;
		list_args.list = NULL;
	}

	log_flag(DATA, "%s: END: list parsing %s{%s(0x%"PRIxPTR")} to List 0x%"PRIxPTR" via parser %s(0x%"PRIxPTR") rc[%d]:%s",
		__func__, path, data_get_type_string(src),
		(uintptr_t) src, (uintptr_t) dst, parser->type_string,
		(uintptr_t) parser, rc, slurm_strerror(rc)
	);

	if (rc)
		*list = NULL;
	FREE_NULL_LIST(list_args.list);
	xfree(path);
	return rc;
}

static int _parse_pointer(const parser_t *const parser, void *dst, data_t *src,
			  args_t *args, data_t *parent_path)
{
	int rc;
	void **ptr = dst;
	const parser_t *const pt = find_parser_by_type(parser->pointer_type);
	bool is_empty_dict = (pt->obj_openapi == OPENAPI_FORMAT_OBJECT) &&
			     (data_get_type(src) == DATA_TYPE_DICT) &&
			     !data_get_dict_length(src);
	bool is_empty_list = (pt->obj_openapi == OPENAPI_FORMAT_ARRAY) &&
			     (data_get_type(src) == DATA_TYPE_LIST) &&
			     !data_get_list_length(src);

	xassert(!*ptr);
	*ptr = alloc_parser_obj(parser);

	if (is_empty_dict || is_empty_list) {
		/*
		 * Detect work around for OpenAPI clients being unable to handle
		 * a null in place of a object/array by placing an empty
		 * dict/array. Place the default allocated object pointer but
		 * skip attempting to parse.
		 */
		return SLURM_SUCCESS;
	}

	if ((rc = parse(*ptr, NO_VAL, pt, src, args, parent_path))) {
		log_flag(DATA, "%s object at 0x%"PRIxPTR" freed due to parser error: %s",
			 pt->obj_type_string, (uintptr_t) ptr,
			 slurm_strerror(rc));
		free_parser_obj(parser, *ptr);
		*ptr = NULL;
	}

	return rc;
}

static data_for_each_cmd_t _foreach_array_entry(data_t *src, void *arg)
{
	int rc;
	foreach_nt_array_t *args = arg;
	void *obj = NULL;
	data_t *ppath = NULL;
	const parser_t *const parser = args->parser;
	const parser_t *const array_parser = args->array_parser;

	xassert(args->magic == MAGIC_FOREACH_NT_ARRAY);
	xassert((args->index > 0) || (args->index == -1));

	if (args->index < 0)
		args->index = 0;

	if (!is_fast_mode(args->args)) {
		data_t *ppath_last;

		ppath = data_copy(NULL, args->parent_path);
		ppath_last = data_get_list_last(ppath);

		/* Use jq style array zero based array notation */
		data_set_string_fmt(ppath_last, "%s[%d]",
				    data_get_string(ppath_last), args->index);
	}

	if (array_parser->model == PARSER_MODEL_NT_PTR_ARRAY)
		obj = alloc_parser_obj(parser);
	else if (array_parser->model == PARSER_MODEL_NT_ARRAY)
		obj = args->sarray + (parser->size * args->index);

	if ((rc = parse(obj, NO_VAL, parser, src, args->args, ppath))) {
		log_flag(DATA, "%s object at 0x%"PRIxPTR" freed due to parser error: %s",
			 parser->obj_type_string, (uintptr_t) obj,
			 slurm_strerror(rc));
		if (array_parser->model == PARSER_MODEL_NT_PTR_ARRAY)
			free_parser_obj(parser, obj);
		FREE_NULL_DATA(ppath);
		return DATA_FOR_EACH_FAIL;
	}

	if (array_parser->model == PARSER_MODEL_NT_PTR_ARRAY) {
		xassert(!args->array[args->index]);
		args->array[args->index] = obj;
	}

	args->index++;

	FREE_NULL_DATA(ppath);
	return DATA_FOR_EACH_CONT;
}

static int _parse_nt_array(const parser_t *const parser, void *dst, data_t *src,
			   args_t *args, data_t *parent_path)
{
	int rc = SLURM_SUCCESS;
	foreach_nt_array_t fargs = {
		.magic = MAGIC_FOREACH_NT_ARRAY,
		.array_parser = parser,
		.parser = find_parser_by_type(parser->array_type),
		.args = args,
		.parent_path = parent_path,
		.index = -1,
	};
	char *path = NULL;

	xassert(args->magic == MAGIC_ARGS);

	if ((data_get_type(src) != DATA_TYPE_LIST) &&
	    (data_convert_type(src, DATA_TYPE_STRING) != DATA_TYPE_STRING)) {
		rc = on_error(PARSING, parser->type, args,
			      ESLURM_DATA_EXPECTED_LIST,
			      set_source_path(&path, args, parent_path),
			      __func__, "Expected List but found a %s",
			      data_get_type_string(src));
		goto cleanup;
	}

	/* assume list can parse all entries */
	if (parser->model == PARSER_MODEL_NT_PTR_ARRAY)
		fargs.array = xcalloc(data_get_list_length(src) + 1,
				      sizeof(*fargs.array));
	else if (parser->model == PARSER_MODEL_NT_ARRAY)
		fargs.sarray = xcalloc(data_get_list_length(src) + 1,
				       fargs.parser->size);

	/* verify new array actually allocated */;
	xassert((fargs.array && (xsize(fargs.array) > 0)) ^
		(fargs.sarray && (xsize(fargs.sarray) > 0)));

	if (data_get_type(src) == DATA_TYPE_LIST) {
		if (data_list_for_each(src, _foreach_array_entry, &fargs) < 0)
			goto cleanup;
	} else if (data_get_type(src) == DATA_TYPE_STRING) {
		/* Assume List is just a single entry */
		if (_foreach_array_entry(src, &fargs) != DATA_FOR_EACH_CONT)
			rc = ESLURM_REST_FAIL_PARSING;
	}

	if (parser->model == PARSER_MODEL_NT_PTR_ARRAY) {
		void ***array_ptr = dst;
		xassert(!*array_ptr);
		SWAP(*array_ptr, fargs.array);
	} else if (parser->model == PARSER_MODEL_NT_ARRAY) {
		void **array_ptr = dst;
		xassert(!*array_ptr);
		SWAP(*array_ptr, fargs.sarray);
	}

cleanup:
	xfree(path);

	if (fargs.array) {
		/* must have failed - cleanup the memory */
		for (int i = 0; fargs.array[i]; i++)
			free_parser_obj(parser, &fargs.array[i]);
		xfree(fargs.array);
	} else if (fargs.sarray) {
		xfree(fargs.sarray);
	}

	return rc;
}

static void _parser_linked_flag(args_t *args, const parser_t *const array,
				const parser_t *const parser, data_t *src,
				void *dst, data_t *parent_path,
				const flag_bit_t *bit, uint64_t *set)
{
	char *path = NULL;
	data_t *ppath = NULL;
	bool matched;

	src = data_resolve_dict_path(src, bit->name);

	if (!is_fast_mode(args)) {
		ppath = data_copy(NULL, parent_path);
		openapi_append_rel_path(ppath, bit->name);
		set_source_path(&path, args, ppath);
	}

	if (!src) {
		matched = false;
	} else if (data_convert_type(src, DATA_TYPE_BOOL) != DATA_TYPE_BOOL) {
		matched = false;

		on_warn(PARSING, parser->type, args, path, __func__,
			"Unable to convert to boolean from %s. Flag %s is being treated as false.",
			data_get_type_string(src), bit->name);

	} else {
		matched = data_get_bool(src);
	}

	if (bit->type == FLAG_BIT_TYPE_BIT)
		_set_flag_bit(parser, dst, bit, matched, path, src);
	else if (bit->type == FLAG_BIT_TYPE_EQUAL) {
		if (matched || ((~(*set) & bit->mask) == bit->mask))
			_set_flag_bit_equal(parser, dst, bit, matched, path,
					    src);
		*set |= bit->mask;
	} else if (bit->type == FLAG_BIT_TYPE_REMOVED) {
		if (matched && !is_fast_mode(args))
			on_warn(PARSING, parser->type, args, path, __func__,
				"Ignoring deprecated flag: %s", bit->name);
	} else {
		fatal_abort("%s: invalid bit_flag_t", __func__);
	}

	log_flag(DATA, "%s: parsed flag %s{%s(0x%" PRIxPTR ")} to %s(0x%" PRIxPTR "+%zd)->%s & 0x%"PRIx64" & %s=0x%"PRIx64" via array parser %s(0x%" PRIxPTR ")=%s(0x%" PRIxPTR ")",
		 __func__, path, data_get_type_string(src),
		 (uintptr_t) src, array->obj_type_string, (uintptr_t) dst,
		 parser->ptr_offset, parser->field_name, bit->mask,
		 bit->flag_name, bit->value, parser->obj_type_string,
		 (uintptr_t) parser, array->type_string, (uintptr_t) array);

	xfree(path);
	FREE_NULL_DATA(ppath);
}

static data_for_each_cmd_t _foreach_parse_marray(const char *key, data_t *data,
						 void *arg)
{
	parse_marray_args_t *aargs = arg;
	parse_marray_args_t cargs = *aargs;
	args_t *args = aargs->args;
	const parser_t *const array = aargs->array;
	char *path = NULL;

	xassert(aargs->magic == MAGIC_FOREACH_PARSE_MARRAY);
	xassert(cargs.magic == MAGIC_FOREACH_PARSE_MARRAY);
	xassert(array->model == PARSER_MODEL_ARRAY);
	xassert(!is_fast_mode(args));

	cargs.parent_path = data_copy(NULL, aargs->parent_path);
	openapi_append_rel_path(cargs.parent_path, key);

	cargs.path = data_copy(NULL, aargs->path);
	data_set_string(data_list_append(cargs.path), key);

	for (int i = 0; i < array->field_count; i++) {
		bool match;
		const parser_t *const parser = &array->fields[i];
		data_t *fpath;

		if (parser->model == PARSER_MODEL_ARRAY_SKIP_FIELD)
			continue;

		if (parser->model ==
		    PARSER_MODEL_ARRAY_LINKED_EXPLODED_FLAG_ARRAY_FIELD) {
			const parser_t *const fp =
				find_parser_by_type(parser->type);

			for (int i = 0; i < fp->flag_bit_array_count; i++) {
				const flag_bit_t *bit = &fp->flag_bit_array[i];

				if (!xstrcasecmp(key, bit->name)) {
					if (slurm_conf.debug_flags &
					    DEBUG_FLAG_DATA) {
						char *p = NULL;
						data_list_join_str(&p,
								   cargs.path,
								   "/");

						log_flag(DATA, "%s: matched %s as bitflag %s",
						      __func__, p, bit->name);

						xfree(p);
					}
					goto cleanup;
				}
			}
		}

		fpath = data_new();
		(void) data_list_split_str(fpath, parser->key, "/");
		match = data_check_match(fpath, cargs.path, false);
		FREE_NULL_DATA(fpath);

		if (match) {
			if (slurm_conf.debug_flags & DEBUG_FLAG_DATA) {
				char *p = NULL;
				data_list_join_str(&p, cargs.path, "/");
				log_flag(DATA, "%s: matched %s to %s",
				      __func__, p, parser->key);
				xfree(p);
			}
			goto cleanup;
		}
	}

	if (data_get_type(data) == DATA_TYPE_DICT) {
		/* still unknown, so try next level of tree */
		(void) data_dict_for_each(data, _foreach_parse_marray, &cargs);
		goto cleanup;
	}

	on_warn(PARSING, array->type, args,
		set_source_path(&path, args, cargs.parent_path),
		__func__, "Ignoring unknown field \"%s\" of type %s in %s", key,
		data_get_type_string(data), array->type_string);

cleanup:
	FREE_NULL_DATA(cargs.path);
	FREE_NULL_DATA(cargs.parent_path);
	xfree(path);
	return DATA_FOR_EACH_CONT;
}

/*
 * Try to guess if the user actually set the value or if the value was dumped
 * with the other oversubsribed values which means it doesn't need to be logged
 */
static int _is_duplicate_linked_parser_value(args_t *args,
					     const parser_t *const array,
					     const parser_t *const parser,
					     data_t *src_obj, data_t *src,
					     data_t *parent_path)
{
	if (parser->field_name_overloads == 1)
		return false;

	for (int i = 0; i < array->field_count; i++) {
		if ((array->fields[i].field_name_overloads != 1) &&
		    !xstrcmp(array->fields[i].field_name, parser->field_name) &&
		    !data_check_match(src,
				      data_key_get(src_obj,
						   array->fields[i].key),
				      false)) {
			return false;
		}
	}

	return true;
}

/* parser linked parser inside of parser array */
static int _parser_linked(args_t *args, const parser_t *const array,
			  const parser_t *const parser, data_t *src, void *dst,
			  data_t *parent_path)
{
	int rc = SLURM_ERROR;
	data_t *ppath = NULL, *src_obj = src;
	char *path = NULL;

	check_parser(parser);
	verify_parser_sliced(parser);

	if (parser->model ==
	    PARSER_MODEL_ARRAY_LINKED_EXPLODED_FLAG_ARRAY_FIELD) {
		const parser_t *const fp = find_parser_by_type(parser->type);
		uint64_t set = 0;

		rc = SLURM_SUCCESS;

		if (parser->ptr_offset != NO_VAL)
			dst += parser->ptr_offset;

		for (int i = 0; i < fp->flag_bit_array_count; i++) {
			const flag_bit_t *bit = &fp->flag_bit_array[i];

			_parser_linked_flag(args, array, fp, src, dst,
					    parent_path, bit, &set);
		}

		goto cleanup;
	}

	if (!is_fast_mode(args))
		ppath = data_copy(NULL, parent_path);

	/* only look for child via key if there was one defined */
	if (parser->key) {
		src = data_resolve_dict_path(src, parser->key);

		if (!is_fast_mode(args))
			openapi_append_rel_path(ppath, parser->key);
	}

	if (parser->model == PARSER_MODEL_ARRAY_REMOVED_FIELD) {
		log_flag(DATA, "%s: skip parsing removed %s object %s(0x%" PRIxPTR ") via parser %s(0x%" PRIxPTR ")",
			__func__, set_source_path(&path, args, ppath),
			parser->obj_type_string, (uintptr_t) dst,
			parser->type_string, (uintptr_t) src);
		rc = SLURM_SUCCESS;
		goto cleanup;
	}

	if (!src) {
		if (parser->required) {
			if ((rc = on_error(PARSING, parser->type, args,
					   ESLURM_DATA_PATH_NOT_FOUND,
					   set_source_path(&path, args, ppath),
					   __func__,
					   "Missing required field '%s' in dictionary",
					   parser->key)))
				goto cleanup;
		} else {
			/* field is missing but not required */
			log_flag(DATA, "%s: skip parsing missing %s to object %s(0x%" PRIxPTR "+%zd)%s%s via parser %s(0x%" PRIxPTR ")",
				__func__, path, parser->obj_type_string,
				(uintptr_t) dst,
				(parser->ptr_offset == NO_VAL ?
					 0 :
					 parser->ptr_offset),
				(parser->field_name ? "->" : ""),
				(parser->field_name ? parser->field_name : ""),
				parser->type_string, (uintptr_t) src);

			rc = SLURM_SUCCESS;
			goto cleanup;
		}
	}

	if (parser->ptr_offset != NO_VAL)
		dst += parser->ptr_offset;

	if (parser->model == PARSER_MODEL_ARRAY_SKIP_FIELD) {
		log_flag(DATA, "%s: SKIP: parsing %s{%s(0x%" PRIxPTR ")} to %s(0x%" PRIxPTR "+%zd)%s%s=%s(0x%" PRIxPTR ") via array parser %s(0x%" PRIxPTR ")=%s(0x%" PRIxPTR ")",
			 __func__, parser->field_name,
			 data_get_type_string(src),
			 (uintptr_t) src, parser->obj_type_string,
			 (uintptr_t) dst, parser->ptr_offset,
			 (parser->field_name ? "->" : ""),
			 (parser->field_name ? parser->field_name : ""),
			 parser->obj_type_string, (uintptr_t) src,
			 parser->type_string, (uintptr_t) array,
			 parser->type_string, (uintptr_t) parser);
		rc = SLURM_SUCCESS;
		goto cleanup;
	}

	xassert(parser->model == PARSER_MODEL_ARRAY_LINKED_FIELD);

	if (!is_fast_mode(args) && parser->deprecated &&
	    (parser->deprecated <= SLURM_MIN_PROTOCOL_VERSION) &&
	    !_is_duplicate_linked_parser_value(args, array, parser, src_obj,
					       src, parent_path)) {
		on_warn(PARSING, parser->type, args,
			set_source_path(&path, args, ppath), __func__,
			"Field \"%s\" is deprecated", parser->key);
	}

	log_flag(DATA, "%s: BEGIN: parsing %s{%s(0x%" PRIxPTR ")} to %s(0x%" PRIxPTR "+%zd)%s%s=%s(0x%" PRIxPTR ") via array parser %s(0x%" PRIxPTR ")=%s(0x%" PRIxPTR ")",
		 __func__, path, data_get_type_string(src),
		 (uintptr_t) src, array->obj_type_string, (uintptr_t) dst,
		 array->ptr_offset, (array->field_name ? "->" : ""),
		 (array->field_name ? array->field_name : ""),
		 parser->obj_type_string, (uintptr_t) src, array->type_string,
		 (uintptr_t) array, parser->type_string, (uintptr_t) parser);

	rc = parse(dst, NO_VAL, find_parser_by_type(parser->type), src, args,
		   ppath);

	log_flag(DATA, "%s: END: parsing %s{%s(0x%" PRIxPTR ")} to %s(0x%" PRIxPTR "+%zd)%s%s=%s(0x%" PRIxPTR ") via array parser %s(0x%" PRIxPTR ")=%s(0x%" PRIxPTR ") rc[%d]:%s",
		 __func__, path, data_get_type_string(src),
		 (uintptr_t) src, array->obj_type_string, (uintptr_t) dst,
		 array->ptr_offset, (array->field_name ? "->" : ""),
		 (array->field_name ? array->field_name : ""),
		 parser->obj_type_string, (uintptr_t) parser,
		 array->type_string, (uintptr_t) array, parser->type_string,
		 (uintptr_t) parser, rc, slurm_strerror(rc));

cleanup:
	FREE_NULL_DATA(ppath);
	xfree(path);
	return rc;
}

static void _parse_check_openapi(const parser_t *const parser, data_t *src,
				 args_t *args, data_t *parent_path)
{
	char *path = NULL;
	const char *oas_type, *oas_format, *found_type, *found_format;
	data_type_t oas_data_type;
	openapi_type_format_t found;

	if (data_get_type(src) == DATA_TYPE_NULL)
		return;

	if (parser->obj_openapi == OPENAPI_FORMAT_INVALID)
		return;

	if (data_get_type(src) ==
	    openapi_type_format_to_data_type(parser->obj_openapi))
		return;

	oas_type = openapi_type_format_to_type_string(parser->obj_openapi);
	oas_format =
		openapi_type_format_to_format_string(parser->obj_openapi);
	found = openapi_data_type_to_type_format(data_get_type(src));
	found_type = openapi_type_format_to_type_string(found);
	found_format = openapi_type_format_to_format_string(found);
	oas_data_type = openapi_type_format_to_data_type(parser->obj_openapi);

	xassert(!is_complex_mode(args));

	/*
	 * Warn as this is user provided data and the parser may accept
	 * the format anyway. Steer the user towards the formats given
	 * in the OpenAPI specification as set in the parser.
	 */
	on_warn(PARSING, parser->type, args,
		set_source_path(&path, args, parent_path), __func__,
		"Expected OpenAPI type=%s%s%s (Slurm type=%s) but got OpenAPI type=%s%s%s (Slurm type=%s): %pd",
		oas_type, (oas_format ? " format=" : ""),
		(oas_format ? oas_format : ""),
		data_type_to_string(oas_data_type), found_type,
		(found_format ? " format=" : ""),
		(found_format ? found_format : ""), data_get_type_string(src),
		src);

	xfree(path);
}

extern int parse(void *dst, ssize_t dst_bytes, const parser_t *const parser,
		 data_t *src, args_t *args, data_t *parent_path)
{
	int rc = SLURM_SUCCESS;
	char *path = NULL;

	check_parser(parser);
	xassert(parser->model != PARSER_MODEL_ARRAY_SKIP_FIELD);
	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(src) != DATA_TYPE_NONE);
	xassert(dst);
	/*
	 * Make sure the target object is the same size since there is no
	 * way to dump value of __typeof__ as a value in C99.
	 */
	xassert((dst_bytes == NO_VAL) || (dst_bytes == parser->size) ||
		(parser->model == PARSER_MODEL_ALIAS));

	if ((rc = load_prereqs(PARSING, parser, args)))
		goto cleanup;

	if (!src) {
		if (parser->required) {
			if ((rc = on_error(PARSING, parser->type, args,
					   ESLURM_DATA_PATH_NOT_FOUND,
					   set_source_path(&path, args,
							   parent_path),
					   __func__,
					   "Missing required field '%s' in dictionary",
					   parser->key)))
				goto cleanup;
		} else {
			/* field is missing but not required */
			log_flag(DATA, "%s: skip parsing missing %s to %zd byte object %s(0x%" PRIxPTR "+%zd)%s%s via parser %s(0x%" PRIxPTR ")",
				__func__, set_source_path(&path, args,
							  parent_path),
				(dst_bytes == NO_VAL ? -1 : dst_bytes),
				parser->obj_type_string, (uintptr_t) dst,
				(parser->ptr_offset == NO_VAL ?
					 0 :
					 parser->ptr_offset),
				(parser->field_name ? "->" : ""),
				(parser->field_name ? parser->field_name : ""),
				parser->type_string, (uintptr_t) parser);

			rc = SLURM_SUCCESS;
			goto cleanup;
		}
	}

	log_flag(DATA, "%s: BEGIN: parsing %s{%s(0x%" PRIxPTR ")} to %zd byte object %s(0x%" PRIxPTR "+%zd)%s%s via parser %s(0x%" PRIxPTR ")",
		 __func__, set_source_path(&path, args, parent_path),
		 data_get_type_string(src),
		 (uintptr_t) src, (dst_bytes == NO_VAL ? -1 : dst_bytes),
		 parser->obj_type_string, (uintptr_t) dst,
		 (parser->ptr_offset == NO_VAL ? 0 : parser->ptr_offset),
		 (parser->field_name ? "->" : ""),
		 (parser->field_name ? parser->field_name : ""),
		 parser->type_string, (uintptr_t) parser);

	switch (parser->model) {
	case PARSER_MODEL_REMOVED:
		if (data_get_type(src) != DATA_TYPE_NULL)
			on_warn(PARSING, parser->type, args, path, __func__,
				"Ignoring value for removed parser");
		rc = SLURM_SUCCESS;
		break;
	case PARSER_MODEL_FLAG_ARRAY:
		verify_parser_not_sliced(parser);
		rc = _parse_flag(dst, parser, src, args, parent_path);
		break;
	case PARSER_MODEL_LIST:
		xassert(parser->list_type > DATA_PARSER_TYPE_INVALID);
		xassert(parser->list_type < DATA_PARSER_TYPE_MAX);
		verify_parser_not_sliced(parser);
		xassert((dst_bytes == NO_VAL) || (dst_bytes == sizeof(list_t *)));
		xassert(!parser->parse);
		rc = _parse_list(parser, dst, src, args, parent_path);
		break;
	case PARSER_MODEL_ARRAY:
	{
		xassert(parser->fields);
		verify_parser_not_sliced(parser);

		if (data_get_type(src) != DATA_TYPE_DICT) {
			rc = on_error(PARSING, parser->type, args,
				      ESLURM_DATA_EXPECTED_DICT,
				      set_source_path(&path, args, parent_path),
				      __func__,
				      "Rejecting %s when dictionary expected",
				      data_get_type_string(src));
		} else {
			/* recursively run the child parsers */
			for (int i = 0; !rc && (i < parser->field_count); i++)
				rc = _parser_linked(args, parser,
						    &parser->fields[i], src,
						    dst, parent_path);

			if (!is_fast_mode(args)) {
				parse_marray_args_t aargs = {
					.magic = MAGIC_FOREACH_PARSE_MARRAY,
					.args = args,
					.array = parser,
					.parent_path = parent_path,
				};

				aargs.path = data_set_list(data_new());
				(void) data_dict_for_each(src,
							  _foreach_parse_marray,
							  &aargs);
				FREE_NULL_DATA(aargs.path);
			}
		}
		break;
	}
	case PARSER_MODEL_PTR:
		verify_parser_not_sliced(parser);
		rc = _parse_pointer(parser, dst, src, args, parent_path);
		break;
	case PARSER_MODEL_NT_PTR_ARRAY:
	case PARSER_MODEL_NT_ARRAY:
		verify_parser_not_sliced(parser);
		rc = _parse_nt_array(parser, dst, src, args, parent_path);
		break;
	case PARSER_MODEL_SIMPLE:
	case PARSER_MODEL_COMPLEX:
		xassert(parser->parse != _parse_list);
		verify_parser_not_sliced(parser);

		if (!is_fast_mode(args) && !is_complex_mode(args))
			_parse_check_openapi(parser, src, args, parent_path);

		rc = parser->parse(parser, dst, src, args, parent_path);
		break;
	case PARSER_MODEL_ALIAS:
		rc = parse(dst, dst_bytes,
			   find_parser_by_type(parser->alias_type), src, args,
			   parent_path);
		break;
	case PARSER_MODEL_ARRAY_LINKED_EXPLODED_FLAG_ARRAY_FIELD:
	case PARSER_MODEL_ARRAY_LINKED_FIELD:
		fatal_abort("%s: link model not allowed %u",
			    __func__, parser->model);
	case PARSER_MODEL_ARRAY_SKIP_FIELD:
		fatal_abort("%s: skip model not allowed %u",
			    __func__, parser->model);
	case PARSER_MODEL_ARRAY_REMOVED_FIELD:
		fatal_abort("%s: removed model not allowed %u",
			    __func__, parser->model);
	case PARSER_MODEL_INVALID:
	case PARSER_MODEL_MAX:
		fatal_abort("%s: invalid model %u", __func__, parser->model);
	}

cleanup:
	log_flag(DATA, "%s: END: parsing %s{%s(0x%" PRIxPTR ")} to %zd byte object %s(0x%" PRIxPTR "+%zd)%s%s via parser %s(0x%" PRIxPTR ") rc[%d]:%s",
		 __func__, set_source_path(&path, args, parent_path),
		 data_get_type_string(src), (uintptr_t) src,
		 (dst_bytes == NO_VAL ? -1 : dst_bytes),
		 parser->obj_type_string, (uintptr_t) dst, (parser->ptr_offset
							    == NO_VAL ? 0 :
							    parser->ptr_offset),
		 (parser->field_name ? "->" : ""), (parser->field_name ?
						    parser->field_name : ""),
		 parser->type_string, (uintptr_t) parser, rc,
		 slurm_strerror(rc));

	xfree(path);

	return rc;
}

static bool _match_flag_bit(const parser_t *const parser, void *src,
			    const flag_bit_t *bit, uint64_t used_equal_bits)
{
	const uint64_t v = bit->mask & bit->value;

	if (used_equal_bits & bit->mask)
		return false;

	/* C allows complier to choose a size for the enum */
	if (parser->size == sizeof(uint64_t)) {
		uint64_t *flags = src;
		return ((*flags & v) == v);
	} else if (parser->size == sizeof(uint32_t)) {
		uint32_t *flags = src;
		return ((*flags & v) == v);
	} else if (parser->size == sizeof(uint16_t)) {
		uint16_t *flags = src;
		return ((*flags & v) == v);
	} else if (parser->size == sizeof(uint8_t)) {
		uint8_t *flags = src;
		return ((*flags & v) == v);
	}

	fatal("%s: unexpected enum size: %zu", __func__, parser->size);
}

static bool _match_flag_equal(const parser_t *const parser, void *src,
			      const flag_bit_t *bit,
			      uint64_t *used_equal_bits_ptr)
{
	bool found;
	const uint64_t v = bit->mask & bit->value;

	/* C allows compiler to choose a size for the enum */
	if (parser->size == sizeof(uint64_t)) {
		uint64_t *flags = src;
		found = ((*flags & bit->mask) == v);
	} else if (parser->size == sizeof(uint32_t)) {
		uint32_t *flags = src;
		found = ((*flags & bit->mask) == v);
	} else if (parser->size == sizeof(uint16_t)) {
		uint16_t *flags = src;
		found = ((*flags & bit->mask) == v);
	} else if (parser->size == sizeof(uint8_t)) {
		uint8_t *flags = src;
		found = ((*flags & bit->mask) == v);
	} else {
		fatal("%s: unexpected enum size: %zu", __func__, parser->size);
	}

	if (found)
		*used_equal_bits_ptr |= bit->mask;

	return found;
}

static void _dump_flag_bit_array_flag(args_t *args, void *src, data_t *dst,
				      const parser_t *const parser,
				      const flag_bit_t *bit, bool set_bool,
				      uint64_t *used_equal_bits)
{
	bool found;

	if (bit->hidden)
		return;

	if (bit->type == FLAG_BIT_TYPE_BIT)
		found = _match_flag_bit(parser, src, bit, *used_equal_bits);
	else if (bit->type == FLAG_BIT_TYPE_EQUAL)
		found = _match_flag_equal(parser, src, bit, used_equal_bits);
	else if (bit->type == FLAG_BIT_TYPE_REMOVED)
		found = false;
	else
		fatal_abort("%s: invalid bit_flag_t", __func__);

	if (set_bool)
		data_set_bool(dst, found);
	else if (found) {
		data_t *dst_flag;

		if (parser->single_flag)
			dst_flag = dst;
		else
			dst_flag = data_list_append(dst);

		data_set_string(dst_flag, bit->name);
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_DATA) {
		const char *type;
		uint64_t value;

		if (parser->size == sizeof(uint64_t)) {
			uint64_t *flags = src;
			value = *flags;
		} else if (parser->size == sizeof(uint32_t)) {
			uint32_t *flags = src;
			value = *flags;
		} else if (parser->size == sizeof(uint16_t)) {
			uint16_t *flags = src;
			value = *flags;
		} else if (parser->size == sizeof(uint8_t)) {
			uint8_t *flags = src;
			value = *flags;
		} else {
			fatal_abort("invalid parser flag size: %zu",
				    parser->size);
		}

		if (bit->type == FLAG_BIT_TYPE_BIT)
			type = "bit";
		else if (bit->type == FLAG_BIT_TYPE_EQUAL)
			type = "bit-equals";
		else if (bit->type == FLAG_BIT_TYPE_REMOVED)
			type = "removed";
		else
			type = "INVALID";

		log_flag(DATA, "%s: %s \"%s\" flag %s %s(%s[0x%"PRIx64"] & %s[0x%"PRIx64"]) & 0x%"PRIx64" = 0x%"PRIx64" for %zd byte %s(0x%" PRIxPTR "+%zd)->%s with parser %s(0x%" PRIxPTR ") to data %s[0x%" PRIxPTR "]",
			 __func__, (found ? "appending matched" : "skipping"),
			 bit->name, type, bit->name, bit->mask_name, bit->mask,
			 bit->flag_name, bit->value, value,
			 (bit->mask & value & bit->value), parser->size,
			 parser->obj_type_string, (uintptr_t) src,
			 parser->ptr_offset, parser->field_name,
			 parser->type_string, (uintptr_t) parser,
			 data_get_type_string(dst),
			 (uintptr_t) dst);
	}
}

static int _dump_flag_bit_array(args_t *args, void *src, data_t *dst,
				const parser_t *const parser)
{
	int rc = SLURM_SUCCESS;
	uint64_t used_equal_bits = 0;

	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	if (!parser->single_flag) {
		if (data_get_type(dst) == DATA_TYPE_NULL)
			data_set_list(dst);
		if (data_get_type(dst) != DATA_TYPE_LIST)
			return ESLURM_DATA_CONV_FAILED;
	}

	for (int8_t i = 0; !rc && (i < parser->flag_bit_array_count); i++)
		_dump_flag_bit_array_flag(args, src, dst, parser,
					  &parser->flag_bit_array[i], false,
					  &used_equal_bits);

	return SLURM_SUCCESS;
}

static int _foreach_dump_list(void *obj, void *arg)
{
	foreach_list_t *args = arg;
	data_t *item = data_list_append(args->dlist);

	xassert(args->magic == MAGIC_FOREACH_LIST);
	xassert(args->args->magic == MAGIC_ARGS);
	check_parser(args->parser);
	xassert(args->parser->ptr_offset == NO_VAL);

	/* we don't know the size of the items in the list */
	if (dump(&obj, NO_VAL, NULL,
		 find_parser_by_type(args->parser->list_type), item,
		 args->args))
		return -1;

	return 0;
}

static int _dump_list(const parser_t *const parser, void *src, data_t *dst,
		      args_t *args)
{
	list_t **list_ptr = src;
	foreach_list_t fargs = {
		.magic = MAGIC_FOREACH_LIST,
		.args = args,
		.parser = parser,
		.list = (list_ptr ? *list_ptr : NULL),
		.dlist = dst,
	};

	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);
	xassert(!list_ptr || !*list_ptr || (list_count(*list_ptr) >= 0));
	xassert((data_get_type(dst) == DATA_TYPE_NULL) ||
		(data_get_type(dst) == DATA_TYPE_LIST));

	if (data_get_type(dst) != DATA_TYPE_LIST)
		data_set_list(dst);

	if (!fargs.list || list_is_empty(fargs.list)) {
		/* list is empty */
		return SLURM_SUCCESS;
	}

	if (list_for_each(fargs.list, _foreach_dump_list, &fargs) < 0) {
		return on_error(DUMPING, parser->type, args, SLURM_ERROR,
				"_foreach_dump_list", __func__,
				"dumping list failed");
	}

	return SLURM_SUCCESS;
}

static int _dump_pointer(const parser_t *const field_parser,
			 const parser_t *const parser, void *src, data_t *dst,
			 args_t *args)
{
	const parser_t *pt = find_parser_by_type(parser->pointer_type);
	void **ptr = src;

	if (!*ptr) {
		if (is_complex_mode(args)) {
			xassert(data_get_type(dst) == DATA_TYPE_NULL);
			return SLURM_SUCCESS;
		}

		/* Fully resolve pointer on NULL to use correct model */
		pt = unalias_parser(pt);

		if (parser->allow_null_pointer ||
		    (field_parser && !field_parser->required)) {
			xassert(data_get_type(dst) == DATA_TYPE_NULL);
		} else if ((pt->model == PARSER_MODEL_ARRAY) ||
			   (pt->obj_openapi == OPENAPI_FORMAT_OBJECT)) {
			/*
			 * OpenAPI clients can't handle a null instead of an
			 * object. Work around by placing an empty dictionary
			 * instead of null.
			 */
			data_set_dict(dst);
		} else if ((pt->model == PARSER_MODEL_LIST) ||
			   (pt->model == PARSER_MODEL_NT_ARRAY) ||
			   (pt->model == PARSER_MODEL_NT_PTR_ARRAY) ||
			   (pt->obj_openapi == OPENAPI_FORMAT_ARRAY)) {
			/*
			 * OpenAPI clients can't handle a null instead of an
			 * array. Work around by placing an empty list instead
			 * of null.
			 */
			data_set_list(dst);
		}

		return SLURM_SUCCESS;
	}

	return dump(*ptr, NO_VAL, NULL, pt, dst, args);
}

static int _dump_nt_array(const parser_t *const parser, void *src, data_t *dst,
			  args_t *args)
{
	int rc = SLURM_SUCCESS;

	data_set_list(dst);

	if (parser->model == PARSER_MODEL_NT_PTR_ARRAY) {
		void ***array_ptr = src;
		void **array;

		if (!(array = *array_ptr))
			return SLURM_SUCCESS;

		for (int i = 0; !rc && array[i]; i++) {
			rc = dump(array[i], NO_VAL, NULL,
				  find_parser_by_type(parser->array_type),
				  data_list_append(dst), args);
		}
	} else if (parser->model == PARSER_MODEL_NT_ARRAY) {
		const parser_t *const ap =
			find_parser_by_type(parser->array_type);
		void **array = src;

		if (!*array)
			return SLURM_SUCCESS;

		for (int i = 0; !rc; i++) {
			bool done = true;
			void *ptr = *array + (ap->size * i);

			/* check every byte of object is zero */
			for (int j = 0; j < ap->size; j++)
				if (((char *) ptr)[j])
					done = false;

			if (done)
				break;

			rc = dump(ptr, NO_VAL, NULL,
				  find_parser_by_type(parser->array_type),
				  data_list_append(dst), args);
		}
	} else {
		fatal_abort("invalid model");
	}

	return rc;
}

static void _dump_removed(const parser_t *parser, data_t *dst, args_t *args)
{
	if (is_complex_mode(args)) {
		data_set_null(dst);
		return;
	}

	while ((parser->model == PARSER_MODEL_ARRAY_REMOVED_FIELD) ||
	       parser->pointer_type) {
		parser = unalias_parser(parser);

		while (parser->model == PARSER_MODEL_ARRAY_REMOVED_FIELD)
			parser = find_parser_by_type(parser->type);
	}

	xassert(parser->model != PARSER_MODEL_ARRAY_REMOVED_FIELD);
	xassert(parser->model !=
		PARSER_MODEL_ARRAY_LINKED_EXPLODED_FLAG_ARRAY_FIELD);
	xassert(parser->model > PARSER_MODEL_INVALID);
	xassert(parser->model < PARSER_MODEL_MAX);

	switch (parser->obj_openapi) {
	case OPENAPI_FORMAT_INT:
	case OPENAPI_FORMAT_INT32:
	case OPENAPI_FORMAT_INT64:
		data_set_int(dst, 0);
		break;
	case OPENAPI_FORMAT_NUMBER:
	case OPENAPI_FORMAT_FLOAT:
	case OPENAPI_FORMAT_DOUBLE:
		data_set_float(dst, 0);
		break;
	case OPENAPI_FORMAT_STRING:
	case OPENAPI_FORMAT_PASSWORD:
		data_set_string(dst, "");
		break;
	case OPENAPI_FORMAT_BOOL:
		data_set_bool(dst, false);
	case OPENAPI_FORMAT_OBJECT:
		data_set_dict(dst);
		break;
	case OPENAPI_FORMAT_ARRAY:
		data_set_list(dst);
		break;
	case OPENAPI_FORMAT_MAX:
	case OPENAPI_FORMAT_INVALID:
		/* Should never happen but avoid crashing clients */
		xassert(false);
		data_set_null(dst);
	};
}

static int _dump_linked(args_t *args, const parser_t *const array,
			const parser_t *const parser, void *src, data_t *dst)
{
	int rc = SLURM_SUCCESS;

	check_parser(parser);
	verify_parser_sliced(parser);

	if ((parser->ptr_offset != NO_VAL) && src)
		src += parser->ptr_offset;

	/*
	 * Only look for child via key if there was one defined
	 */
	if (parser->key) {
		/*
		 * Detect duplicate keys
		 */
		xassert(!data_resolve_dict_path(dst, parser->key));
		dst = data_define_dict_path(dst, parser->key);
	}

	xassert(dst && (data_get_type(dst) != DATA_TYPE_NONE));

	if (parser->model == PARSER_MODEL_ARRAY_SKIP_FIELD) {
		log_flag(DATA, "SKIP: %s parser %s->%s(0x%" PRIxPTR ") for %s(0x%" PRIxPTR ")->%s(+%zd) for data(0x%" PRIxPTR ")/%s(0x%" PRIxPTR ")",
			 parser->obj_type_string,
			 array->type_string,
			 parser->type_string, (uintptr_t)
			 parser, array->obj_type_string,
			 (uintptr_t) src, array->field_name,
			 array->ptr_offset, (uintptr_t) dst,
			 array->key, (uintptr_t) dst);
		return SLURM_SUCCESS;
	}

	if (parser->model == PARSER_MODEL_ARRAY_REMOVED_FIELD) {
		const parser_t *const rparser =
			find_parser_by_type(parser->type);

		log_flag(DATA, "removed: %s parser %s->%s(0x%" PRIxPTR ") for %s(0x%" PRIxPTR ") for data(0x%" PRIxPTR ")/%s(0x%" PRIxPTR ")",
			 parser->obj_type_string,
			 array->type_string,
			 parser->type_string, (uintptr_t)
			 parser, array->obj_type_string,
			 (uintptr_t) src, (uintptr_t) dst,
			 array->key, (uintptr_t) dst);

		_dump_removed(rparser, dst, args);

		return SLURM_SUCCESS;
	}

	if (parser->model ==
	    PARSER_MODEL_ARRAY_LINKED_EXPLODED_FLAG_ARRAY_FIELD) {
		uint64_t used_equal_bits = 0;

		if (data_get_type(dst) == DATA_TYPE_NULL)
			data_set_dict(dst);

		for (int i = 0; i < parser->flag_bit_array_count; i++) {
			const flag_bit_t *bit = &parser->flag_bit_array[i];

			if (bit->hidden)
				continue;

			data_t *bit_dst = data_define_dict_path(dst, bit->name);

			xassert(src);
			_dump_flag_bit_array_flag(args, src, bit_dst, parser,
						  bit, true, &used_equal_bits);
		}

		return SLURM_SUCCESS;
	}

	xassert(parser->model == PARSER_MODEL_ARRAY_LINKED_FIELD);

	log_flag(DATA, "BEGIN: dumping %s parser %s->%s(0x%" PRIxPTR ") for %s(0x%" PRIxPTR ")->%s(+%zd) for data(0x%" PRIxPTR ")/%s(0x%" PRIxPTR ")",
		 parser->obj_type_string, array->type_string,
		 parser->type_string, (uintptr_t) parser,
		 parser->obj_type_string, (uintptr_t) src, array->field_name,
		 array->ptr_offset, (uintptr_t) dst, array->key,
		 (uintptr_t) dst);

	rc = dump(src, NO_VAL, parser, find_parser_by_type(parser->type), dst,
		  args);

	log_flag(DATA, "END: dumping %s parser %s->%s(0x%" PRIxPTR ") for %s(0x%" PRIxPTR ")->%s(+%zd) for data(0x%" PRIxPTR ")/%s(0x%" PRIxPTR ")",
		 parser->obj_type_string, array->type_string,
		 parser->type_string, (uintptr_t) parser,
		 array->obj_type_string, (uintptr_t) src, array->field_name,
		 array->ptr_offset, (uintptr_t) dst, array->key,
		 (uintptr_t) dst);

	return rc;
}

static void _check_dump(const parser_t *const parser, data_t *dst, args_t *args)
{
	/*
	 * Resultant dump must be the proscribed OpenAPI compatible data_t type.
	 * Anything else will break most generated OpenAPI clients.
	 */
	if (parser->obj_openapi == OPENAPI_FORMAT_INVALID)
		return;

	if (!is_complex_mode(args)) {
		xassert(data_get_type(dst) ==
			openapi_type_format_to_data_type(parser->obj_openapi));
	}
}

extern int dump(void *src, ssize_t src_bytes,
		const parser_t *const field_parser,
		const parser_t *const parser, data_t *dst, args_t *args)
{
	int rc;

	log_flag(DATA, "dump %zd byte %s object at 0x%" PRIxPTR " with parser %s(0x%" PRIxPTR ") to data 0x%" PRIxPTR,
		 (src_bytes == NO_VAL ? -1 : src_bytes),
		 parser->obj_type_string, (uintptr_t) src, parser->type_string,
		 (uintptr_t) parser, (uintptr_t) dst);

	check_parser(parser);
	xassert(parser->model != PARSER_MODEL_ARRAY_SKIP_FIELD);
	xassert(dst && (data_get_type(dst) != DATA_TYPE_NONE));
	xassert(args->magic == MAGIC_ARGS);
	xassert((src_bytes == NO_VAL) || (src_bytes > 0));

	/*
	 * Make sure the source object is the same size since there is no
	 * way to dump value of __typeof__ as a value in C.
	 */
	xassert((src_bytes == NO_VAL) || (src_bytes == parser->size) ||
		(parser->model == PARSER_MODEL_ALIAS));

	if (args->flags & FLAG_SPEC_ONLY) {
		set_openapi_schema(dst, parser, args);
		return SLURM_SUCCESS;
	}

	if ((rc = load_prereqs(DUMPING, parser, args)))
		goto done;

	switch (parser->model) {
	case PARSER_MODEL_REMOVED:
		_dump_removed(parser, dst, args);
		rc = SLURM_SUCCESS;
		break;
	case PARSER_MODEL_FLAG_ARRAY:
		verify_parser_not_sliced(parser);
		xassert((data_get_type(dst) == DATA_TYPE_NULL) ||
			(data_get_type(dst) == DATA_TYPE_LIST));
		xassert(parser->ptr_offset == NO_VAL);

		if (data_get_type(dst) != DATA_TYPE_LIST)
			data_set_list(dst);

		rc = _dump_flag_bit_array(args, src, dst, parser);
		break;
	case PARSER_MODEL_ARRAY:
		verify_parser_not_sliced(parser);
		xassert(parser->fields);
		xassert((data_get_type(dst) == DATA_TYPE_NULL) ||
			(data_get_type(dst) == DATA_TYPE_DICT));
		/* recursively run linked parsers for each struct field */
		for (int i = 0; !rc && (i < parser->field_count); i++)
			rc = _dump_linked(args, parser, &parser->fields[i], src,
					  dst);
		break;
	case PARSER_MODEL_LIST:
		xassert(parser->list_type > DATA_PARSER_TYPE_INVALID);
		xassert(parser->list_type < DATA_PARSER_TYPE_MAX);
		verify_parser_not_sliced(parser);
		xassert((data_get_type(dst) == DATA_TYPE_NULL) ||
			(data_get_type(dst) == DATA_TYPE_LIST));
		xassert((src_bytes == NO_VAL) || (src_bytes == sizeof(list_t *)));
		xassert(!parser->dump);
		rc = _dump_list(parser, src, dst, args);
		break;
	case PARSER_MODEL_PTR:
		xassert(parser->pointer_type > DATA_PARSER_TYPE_INVALID);
		xassert(parser->pointer_type < DATA_PARSER_TYPE_MAX);
		verify_parser_not_sliced(parser);
		xassert(data_get_type(dst) == DATA_TYPE_NULL);

		rc = _dump_pointer(field_parser, parser, src, dst, args);
		break;
	case PARSER_MODEL_NT_PTR_ARRAY:
	case PARSER_MODEL_NT_ARRAY:
		xassert(parser->array_type > DATA_PARSER_TYPE_INVALID);
		xassert(parser->array_type < DATA_PARSER_TYPE_MAX);
		verify_parser_not_sliced(parser);
		xassert(data_get_type(dst) == DATA_TYPE_NULL);

		rc = _dump_nt_array(parser, src, dst, args);
		break;
	case PARSER_MODEL_SIMPLE:
	case PARSER_MODEL_COMPLEX:
		xassert(data_get_type(dst) == DATA_TYPE_NULL);
		verify_parser_not_sliced(parser);

		xassert(parser->dump != _dump_list);

		/*
		 * parser->pointer_type and parser->array_type may be set but
		 * they are only used to OpenAPI typing and are ignored here.
		 */

		rc = parser->dump(parser, src, dst, args);
		_check_dump(parser, dst, args);
		break;
	case PARSER_MODEL_ALIAS:
		rc = dump(src, src_bytes, NULL,
			  find_parser_by_type(parser->alias_type), dst, args);
		break;
	case PARSER_MODEL_ARRAY_LINKED_EXPLODED_FLAG_ARRAY_FIELD:
	case PARSER_MODEL_ARRAY_LINKED_FIELD:
		fatal_abort("%s: link model not allowed %u",
			    __func__, parser->model);
	case PARSER_MODEL_ARRAY_SKIP_FIELD:
		fatal_abort("%s: skip model not allowed %u",
			    __func__, parser->model);
	case PARSER_MODEL_ARRAY_REMOVED_FIELD:
		fatal_abort("%s: removed model not allowed %u",
			    __func__, parser->model);
	case PARSER_MODEL_INVALID:
	case PARSER_MODEL_MAX:
		fatal_abort("%s: invalid model %u", __func__, parser->model);
	}

done:
	log_flag(DATA, "dump %zd byte %s object at 0x%" PRIxPTR " with parser %s(0x%" PRIxPTR ") to data 0x%" PRIxPTR " rc[%d]=%s",
		 (src_bytes == NO_VAL ? -1 : src_bytes),
		 parser->obj_type_string, (uintptr_t) src, parser->type_string,
		 (uintptr_t) parser, (uintptr_t) dst, rc, slurm_strerror(rc));

	return rc;
}
