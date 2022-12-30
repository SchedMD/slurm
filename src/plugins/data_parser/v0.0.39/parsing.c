/*****************************************************************************\
 *  parsers.c - Slurm data parsers
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
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
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "api.h"
#include "events.h"
#include "parsers.h"
#include "parsing.h"
#include "slurmdb_helpers.h"

#define MAGIC_FOREACH_LIST_FLAG 0xa1d4acd2
#define MAGIC_FOREACH_LIST 0xaefa2af3
#define PATH_SEP "."

typedef struct {
	int magic;
	ssize_t index;
	args_t *args;
	const parser_t *const parser;
	List list;
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
} foreach_flag_parser_args_t;

static void _set_flag_bit(const parser_t *const parser, void *dst,
			  const flag_bit_t *bit, bool matched, const char *path,
			  data_t *src)
{
	/* C allows complier to choose a size for the enum */
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
	/* C allows complier to choose a size for the enum
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

static data_for_each_cmd_t _foreach_flag_parser(data_t *src, void *arg)
{
	foreach_flag_parser_args_t *args = arg;
	void *dst = args->dst;
	const parser_t *const parser = args->parser;
	bool matched = !xstrcasecmp(data_get_string(src), parser->flag_name);
	char *path = NULL;

	xassert(args->magic == MAGIC_FOREACH_LIST_FLAG);
	xassert(args->args->magic == MAGIC_ARGS);
	xassert(parser->magic == MAGIC_PARSER);

	if (slurm_conf.debug_flags & DEBUG_FLAG_DATA) {
		/*
		 * This is a terminal leaf so we don't always need to update the
		 * parent_path unless DATA logging is active
		 */
		data_t *ppath = data_copy(NULL, args->parent_path);
		data_t *ppath_last = data_get_list_last(ppath);

		if (args->index < 0)
			args->index = 0;

		/* Use jq style array zero based array notation */
		data_set_string_fmt(ppath_last, "%s[%zu]",
				    data_get_string(ppath_last), args->index);

		args->index++;
		(void) data_list_join_str(&path, ppath, PATH_SEP);
		FREE_NULL_DATA(ppath);
	}

	if (parser->flag == FLAG_TYPE_BIT_ARRAY) {
		for (int8_t i = 0; (i < parser->flag_bit_array_count); i++) {
			const flag_bit_t *bit = &parser->flag_bit_array[i];

			if (bit->type == FLAG_BIT_TYPE_EQUAL)
				_set_flag_bit(parser, dst, bit, matched, path,
					      src);
			else if (bit->type == FLAG_BIT_TYPE_BIT)
				_set_flag_bit_equal(parser, dst, bit, matched,
						    path, src);
			else
				fatal_abort("%s: invalid bit_flag_t", __func__);
		}
	} else if (parser->flag == FLAG_TYPE_BOOL) {
		/*
		 * match size exactly of source to avoid any high bits
		 * not getting cleared
		 */
		if (parser->size == sizeof(uint64_t)) {
			uint64_t *flags = dst;
			*flags = (matched ? 1 : 0);
		} else if (parser->size == sizeof(uint32_t)) {
			uint32_t *flags = dst;
			*flags = (matched ? 1 : 0);
		} else if (parser->size == sizeof(uint16_t)) {
			uint16_t *flags = dst;
			*flags = (matched ? 1 : 0);
		} else if (parser->size == sizeof(uint8_t)) {
			uint8_t *flags = dst;
			*flags = (matched ? 1 : 0);
		} else {
			fatal_abort("%s: unexpected enum size: %zu", __func__,
				    parser->size);
		}

		log_flag(DATA, "%s: %s{%s(0x%" PRIxPTR ")} %s %s %s %s(0x%" PRIxPTR "+%zd)%s%s=%s via boolean flag parser %s(0x%" PRIxPTR ")",
			 __func__, path,
			 data_type_to_string(data_get_type(src)),
			 (uintptr_t) src, (matched ? "==" : "!="),
			 parser->flag_name,
			 (matched ? "setting" : "not setting"),
			 parser->obj_type_string, (uintptr_t) dst,
			 parser->ptr_offset, (parser->field_name ? "->" : ""),
			 (parser->field_name ? parser->field_name : ""),
			 (matched ? "true" : "false"), parser->obj_type_string,
			 (uintptr_t) parser);
	} else {
		fatal_abort("%s: invalid flag type: 0x%d", __func__,
			    parser->flag);
	}

	xfree(path);
	return DATA_FOR_EACH_CONT;
}

static int parse_flag(void *dst, const parser_t *const parser, data_t *src,
		      args_t *args, data_t *parent_path)
{
	int rc = SLURM_SUCCESS;
	char *path = NULL;
	data_t *ppath = data_copy(NULL, parent_path);
	foreach_flag_parser_args_t fargs = {
		.magic = MAGIC_FOREACH_LIST_FLAG,
		.args = args,
		.parser = parser,
		.dst = dst,
		.parent_path = ppath,
		.index = -1,
	};

	xassert(parser->key[0]);
	xassert(args->magic == MAGIC_ARGS);
	xassert(parser->magic == MAGIC_PARSER);
	xassert((parser->flag == FLAG_TYPE_BIT_ARRAY) ||
		(parser->flag == FLAG_TYPE_BOOL));

	if (parser->ptr_offset > 0)
		fargs.dst += parser->ptr_offset;

	if (slurm_conf.debug_flags & DEBUG_FLAG_DATA)
		(void) data_list_join_str(&path, ppath, PATH_SEP);

	if (data_get_type(src) != DATA_TYPE_LIST) {
		if (!path)
			(void) data_list_join_str(&path, ppath, PATH_SEP);

		rc = on_error(PARSING, parser->type, args,
			      ESLURM_DATA_FLAGS_INVALID_TYPE, path, __func__,
			      "Expected a List but found a %s",
			      data_type_to_string(data_get_type(src)));
	}

	/*
	 * Flags need special handling as they are always a LIST with a
	 * matching string value. This requires that each possible flag
	 * must be searched for in the list to know if it is there or
	 * not.
	 */
	if (data_list_for_each(src, _foreach_flag_parser, &fargs) < 0) {
		if (!path)
			(void) data_list_join_str(&path, ppath, PATH_SEP);

		rc = on_error(PARSING, parser->type, args,
			      ESLURM_DATA_FLAGS_INVALID, path, __func__,
			      "Parsing flags failed");
		goto cleanup;
	}

cleanup:
	FREE_NULL_DATA(ppath);
	xfree(path);
	return rc;
}

static data_for_each_cmd_t _foreach_parse_list(data_t *src, void *arg)
{
	int rc;
	foreach_list_t *args = arg;
	const parser_t *const parser = args->parser;
	const parser_t *const lparser = find_parser_by_type(parser->list_type);
	ssize_t size = 0; /* set by list_new_func() */
	void *obj = parser->list_new_func(lparser, &size);
	data_t *ppath = data_copy(NULL, args->parent_path);
	data_t *ppath_last = data_get_list_last(ppath);

	xassert(args->magic == MAGIC_FOREACH_LIST);
	check_parser(parser);
	check_parser(lparser);
	xassert(!args->dlist); /* only for dumping */
	xassert(size > 0);
	xassert((args->index > 0) || (args->index == -1));
	xassert((lparser->size == NO_VAL) || (xsize(obj) == lparser->size));
	xassert(size == xsize(obj));

	if (args->index < 0)
		args->index = 0;

	/* Use jq style array zero based array notation */
	data_set_string_fmt(ppath_last, "%s[%zu]",
			    data_get_string(ppath_last),
			    args->index);

	if ((rc = parse(obj, size, lparser, src, args->args, ppath))) {
		log_flag(DATA, "%zd byte %s object at 0x%"PRIxPTR" freed due to parser error: %s",
			 size, lparser->obj_type_string, (uintptr_t) obj,
			 slurm_strerror(rc));
		xassert(size == xsize(obj));
		parser->list_del_func(obj);
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
	List *list = dst;
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

	if (slurm_conf.debug_flags & DEBUG_FLAG_DATA)
		(void) data_list_join_str(&path, parent_path, PATH_SEP);

	log_flag(DATA, "%s: BEGIN: list parsing %s{%s(0x%"PRIxPTR")} to List 0x%"PRIxPTR" via parser %s(0x%"PRIxPTR")",
		__func__, path, data_type_to_string(data_get_type(src)),
		(uintptr_t) src, (uintptr_t) dst, parser->type_string,
		(uintptr_t) parser
	);

	if (!list_args.list)
		list_args.list = list_create(parser->list_del_func);

	xassert(list_count(list_args.list) >= 0);

	if (data_get_type(src) != DATA_TYPE_LIST) {
		rc = on_error(PARSING, parser->type, args,
			      ESLURM_DATA_FLAGS_INVALID_TYPE, path, __func__,
			      "Expected List but found a %s",
			      data_type_to_string(data_get_type(src)));
		goto cleanup;
	}

	if (data_list_for_each(src, _foreach_parse_list, &list_args) < 0) {
		if (!path)
			(void) data_list_join_str(&path, parent_path, PATH_SEP);
		rc = on_error(PARSING, parser->type, args,
			      ESLURM_REST_FAIL_PARSING, path, __func__,
			      "parsing failed");
		goto cleanup;
	}

	if (!rc)
		*list = list_args.list;

cleanup:
	log_flag(DATA, "%s: END: list parsing %s{%s(0x%"PRIxPTR")} to List 0x%"PRIxPTR" via parser %s(0x%"PRIxPTR") rc[%d]:%s",
		__func__, path, data_type_to_string(data_get_type(src)),
		(uintptr_t) src, (uintptr_t) dst, parser->type_string,
		(uintptr_t) parser, rc, slurm_strerror(rc)
	);

	xfree(path);
	return rc;
}

extern int parse(void *dst, ssize_t dst_bytes, const parser_t *const parser,
		 data_t *src, args_t *args, data_t *parent_path)
{
	int rc;
	data_t *pd = NULL, *ppath = data_copy(NULL, parent_path);
	char *path = NULL;

	if (slurm_conf.debug_flags & DEBUG_FLAG_DATA)
		(void) data_list_join_str(&path, ppath, PATH_SEP);

	check_parser(parser);
	xassert(!parser->skip);
	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(src) != DATA_TYPE_NONE);
	xassert(dst);
	/*
	 * Make sure the target object is the same size since there is no
	 * way to dump value of __typeof__ as a value in C99.
	 */
	xassert((parser->size == NO_VAL) || (dst_bytes == NO_VAL) ||
		(dst_bytes == parser->size));

	if ((rc = load_prereqs(PARSING, parser, args)))
		goto cleanup;

	/* only look for child via key if there was one defined */
	if (parser->key) {
		pd = data_resolve_dict_path(src, parser->key);

		(void) data_list_split_str(ppath, parser->key, PATH_SEP);

		if (slurm_conf.debug_flags & DEBUG_FLAG_DATA) {
			/* update path */
			xfree(path);
			(void) data_list_join_str(&path, ppath, PATH_SEP);
		}
	} else {
		pd = src;
	}

	if (!pd) {
		if (parser->required) {
			if (!path)
				(void) data_list_join_str(&path, ppath,
							  PATH_SEP);

			if ((rc = on_error(PARSING, parser->type, args,
					   ESLURM_DATA_PATH_NOT_FOUND, path, __func__,
					   "Missing required field '%s' in dictionary",
					   parser->key)))
				goto cleanup;
		} else {
			/* field is missing but not required */
			log_flag(DATA, "%s: skip parsing missing %s to %zd byte object %s(0x%" PRIxPTR "+%zd)%s%s via parser %s(0x%" PRIxPTR ")",
				__func__, path,
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
		 __func__, path, data_type_to_string(data_get_type(pd)),
		 (uintptr_t) pd, (dst_bytes == NO_VAL ? -1 : dst_bytes),
		 parser->obj_type_string, (uintptr_t) dst,
		 (parser->ptr_offset == NO_VAL ? 0 : parser->ptr_offset),
		 (parser->field_name ? "->" : ""),
		 (parser->field_name ? parser->field_name : ""),
		 parser->type_string, (uintptr_t) parser);

	if (parser->flag != FLAG_TYPE_NONE) {
		verify_parser_sliced(parser);
		rc = parse_flag(dst, parser, pd, args, ppath);
		goto cleanup;
	}

	if (parser->fields) {
		verify_parser_not_sliced(parser);

		/* recursively run the child parsers */
		for (int i = 0; !rc && (i < parser->field_count); i++) {
			const parser_t *const pchild = &parser->fields[i];
			void *schild = dst;

			check_parser(pchild);
			verify_parser_sliced(pchild);
			if (pchild->skip) {
				log_flag(DATA, "%s: SKIP: parsing %s{%s(0x%" PRIxPTR ")} to %s(0x%" PRIxPTR "+%zd)%s%s=%s(0x%" PRIxPTR ") via array parser %s(0x%" PRIxPTR ")[%d]=%s(0x%" PRIxPTR ")",
					 __func__, pchild->field_name,
					 data_type_to_string(data_get_type(pd)),
					 (uintptr_t) pd,
					 parser->obj_type_string,
					 (uintptr_t) dst, parser->ptr_offset,
					 (parser->field_name ? "->" : ""),
					 (parser->field_name ?
						  parser->field_name :
						  ""),
					 pchild->obj_type_string,
					 (uintptr_t) schild,
					 parser->type_string,
					 (uintptr_t) parser, i,
					 pchild->type_string,
					 (uintptr_t) pchild);

				continue;
			}

			if (parser->ptr_offset != NO_VAL)
				schild += parser->ptr_offset;

			log_flag(DATA, "%s: BEGIN: parsing %s{%s(0x%" PRIxPTR ")} to %s(0x%" PRIxPTR "+%zd)%s%s=%s(0x%" PRIxPTR ") via array parser %s(0x%" PRIxPTR ")[%d]=%s(0x%" PRIxPTR ")",
				 __func__, path,
				 data_type_to_string(data_get_type(pd)),
				 (uintptr_t) pd, parser->obj_type_string,
				 (uintptr_t) dst, parser->ptr_offset,
				 (parser->field_name ? "->" : ""),
				 (parser->field_name ? parser->field_name : ""),
				 pchild->obj_type_string, (uintptr_t) schild,
				 parser->type_string, (uintptr_t) parser, i,
				 pchild->type_string, (uintptr_t) pchild);

			rc = parse(schild, NO_VAL, pchild, pd, args, ppath);

			log_flag(DATA, "%s: END: parsing %s{%s(0x%" PRIxPTR ")} to %s(0x%" PRIxPTR "+%zd)%s%s=%s(0x%" PRIxPTR ") via array parser %s(0x%" PRIxPTR ")[%d]=%s(0x%" PRIxPTR ") rc[%d]:%s",
				 __func__, path,
				 data_type_to_string(data_get_type(pd)),
				 (uintptr_t) pd, parser->obj_type_string,
				 (uintptr_t) dst, parser->ptr_offset,
				 (parser->field_name ? "->" : ""),
				 (parser->field_name ? parser->field_name : ""),
				 pchild->obj_type_string, (uintptr_t) schild,
				 parser->type_string, (uintptr_t) parser, i,
				 pchild->type_string, (uintptr_t) pchild, rc,
				 slurm_strerror(rc));
		}

		goto cleanup;
	}

	if (parser->list_type) {
		verify_parser_not_sliced(parser);
		xassert((dst_bytes == NO_VAL) || (dst_bytes == sizeof(List)));
		xassert(!parser->parse);
		rc = _parse_list(parser, dst, pd, args, ppath);
		goto cleanup;
	}
	xassert(parser->parse != _parse_list);

	if (!parser->parse) {
		const parser_t *const pchild =
			find_parser_by_type(parser->type);
		void *schild = dst;

		verify_parser_not_sliced(pchild);
		check_parser(pchild);

		if (parser->ptr_offset != NO_VAL)
			schild += parser->ptr_offset;

		xassert(!xstrcmp(parser->type_string, pchild->type_string));
		log_flag(DATA, "%s: BEGIN: parsing %s{%s(0x%" PRIxPTR ")} to %s(0x%" PRIxPTR "+%zd)%s%s=%s(0x%" PRIxPTR ") via linked parser %s(0x%" PRIxPTR "->0x%" PRIxPTR ")",
			 __func__, path, data_type_to_string(data_get_type(pd)),
			 (uintptr_t) pd, parser->obj_type_string,
			 (uintptr_t) dst, parser->ptr_offset,
			 (parser->field_name ? "->" : ""),
			 (parser->field_name ? parser->field_name : ""),
			 pchild->obj_type_string, (uintptr_t) schild,
			 parser->type_string, (uintptr_t) parser,
			 (uintptr_t) pchild);

		rc = parse(schild, NO_VAL, pchild, pd, args, ppath);

		log_flag(DATA, "%s: END: parsing %s{%s(0x%" PRIxPTR ")} to %s(0x%" PRIxPTR "+%zd)%s%s=%s(0x%" PRIxPTR ") via linked parser %s(0x%" PRIxPTR "->0x%" PRIxPTR ") rc[%d]:%s",
			 __func__, path, data_type_to_string(data_get_type(pd)),
			 (uintptr_t) pd, parser->obj_type_string,
			 (uintptr_t) dst, parser->ptr_offset,
			 (parser->field_name ? "->" : ""),
			 (parser->field_name ? parser->field_name : ""),
			 pchild->obj_type_string, (uintptr_t) schild,
			 parser->type_string, (uintptr_t) parser,
			 (uintptr_t) pchild, rc, slurm_strerror(rc));

		goto cleanup;
	}

	verify_parser_not_sliced(parser);

	/* must be a simple or complex type */
	rc = parser->parse(parser, dst, pd, args, ppath);

cleanup:
	log_flag(DATA, "%s: END: parsing %s{%s(0x%" PRIxPTR ")} to %zd byte object %s(0x%" PRIxPTR "+%zd)%s%s via parser %s(0x%" PRIxPTR ") rc[%d]:%s",
		 __func__, path, data_type_to_string(data_get_type(pd)),
		 (uintptr_t) pd, (dst_bytes == NO_VAL ? -1 : dst_bytes),
		 parser->obj_type_string, (uintptr_t) dst,
		 (parser->ptr_offset == NO_VAL ? 0 : parser->ptr_offset),
		 (parser->field_name ? "->" : ""),
		 (parser->field_name ? parser->field_name : ""),
		 parser->type_string, (uintptr_t) parser, rc,
		 slurm_strerror(rc));

	FREE_NULL_DATA(ppath);
	xfree(path);

	return rc;
}

static int _dump_flag_bool(args_t *args, void *src, data_t *dst,
			   const parser_t *const parser)
{
	bool found = false;

	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	if (data_get_type(dst) == DATA_TYPE_NULL)
		data_set_list(dst);
	if (data_get_type(dst) != DATA_TYPE_LIST)
		return ESLURM_DATA_CONV_FAILED;

	if (parser->size == sizeof(uint64_t)) {
		uint64_t *ptr = src;
		found = !!*ptr;
	} else if (parser->size == sizeof(uint32_t)) {
		uint32_t *ptr = src;
		found = !!*ptr;
	} else if (parser->size == sizeof(uint16_t)) {
		uint16_t *ptr = src;
		found = !!*ptr;
	} else if (parser->size == sizeof(uint8_t)) {
		uint8_t *ptr = src;
		found = !!*ptr;
	} else {
		fatal("%s: unexpected bool size: %zu", __func__, parser->size);
	}

	if (found)
		data_set_string(data_list_append(dst), parser->flag_name);

	return SLURM_SUCCESS;
}

static bool _match_flag_bit(const parser_t *const parser, void *src,
			    const flag_bit_t *bit)
{
	const uint64_t v = bit->mask & bit->value;

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
			      const flag_bit_t *bit)
{
	const uint64_t v = bit->mask & bit->value;

	/* C allows complier to choose a size for the enum */
	if (parser->size == sizeof(uint64_t)) {
		uint64_t *flags = src;
		return ((*flags & bit->mask) == v);
	} else if (parser->size == sizeof(uint32_t)) {
		uint32_t *flags = src;
		return ((*flags & bit->mask) == v);
	} else if (parser->size == sizeof(uint16_t)) {
		uint16_t *flags = src;
		return ((*flags & bit->mask) == v);
	} else if (parser->size == sizeof(uint8_t)) {
		uint8_t *flags = src;
		return ((*flags & bit->mask) == v);
	}

	fatal("%s: unexpected enum size: %zu", __func__, parser->size);
}

static int _dump_flag_bit_array(args_t *args, void *src, data_t *dst,
				const parser_t *const parser)
{
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	if (data_get_type(dst) == DATA_TYPE_NULL)
		data_set_list(dst);
	if (data_get_type(dst) != DATA_TYPE_LIST)
		return ESLURM_DATA_CONV_FAILED;

	for (int8_t i = 0; !rc && (i < parser->flag_bit_array_count); i++) {
		bool found;
		const flag_bit_t *bit = &parser->flag_bit_array[i];

		if (bit->type == FLAG_BIT_TYPE_BIT)
			found = _match_flag_bit(parser, src, bit);
		else if (bit->type == FLAG_BIT_TYPE_EQUAL)
			found = _match_flag_equal(parser, src, bit);
		else
			fatal_abort("%s: invalid bit_flag_t", __func__);

		if (found)
			data_set_string(data_list_append(dst), bit->name);

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
			else
				type = "INVALID";

			log_flag(DATA, "%s: %s \"%s\" flag %s %s(%s[0x%"PRIx64"] & %s[0x%"PRIx64"]) & 0x%"PRIx64" = 0x%"PRIx64" for %zd byte %s(0x%" PRIxPTR "+%zd)->%s with parser %s(0x%" PRIxPTR ") to data %s[0x%" PRIxPTR "]",
				 __func__, (found ? "appending matched" : "skipping"),
				 bit->name, type, bit->name, bit->mask_name,
				 bit->mask, bit->flag_name, bit->value, value,
				 (bit->mask & value & bit->value),
				 parser->size, parser->obj_type_string,
				 (uintptr_t) src, parser->ptr_offset,
				 parser->field_name, parser->type_string,
				 (uintptr_t) parser,
				 data_type_to_string(data_get_type(dst)),
				 (uintptr_t) dst);
		}
	}

	return SLURM_SUCCESS;
}

static int dump_flag(void *src, const parser_t *const parser, data_t *dst,
		     args_t *args)
{
	void *obj = src + parser->ptr_offset;

	xassert((parser->ptr_offset == NO_VAL) || (parser->ptr_offset >= 0));
	check_parser(parser);
	xassert(args->magic == MAGIC_ARGS);
	xassert((parser->flag == FLAG_TYPE_BIT_ARRAY) ||
		(parser->flag == FLAG_TYPE_BOOL));

	if (data_get_type(dst) != DATA_TYPE_LIST) {
		xassert(data_get_type(dst) == DATA_TYPE_NULL);
		data_set_list(dst);
	}

	switch (parser->flag) {
	case FLAG_TYPE_BOOL:
		return _dump_flag_bool(args, obj, dst, parser);
	case FLAG_TYPE_BIT_ARRAY:
		return _dump_flag_bit_array(args, obj, dst, parser);
	default :
		fatal("%s: invalid flag type: 0x%d", __func__, parser->flag);
	}
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
	if (data_parser_p_dump(args->args, args->parser->list_type, obj, NO_VAL,
			       item))
		return -1;

	return 0;
}

static int _dump_list(const parser_t *const parser, void *src, data_t *dst,
		      args_t *args)
{
	List *list_ptr = src;
	foreach_list_t fargs = {
		.magic = MAGIC_FOREACH_LIST,
		.args = args,
		.parser = parser,
		.list = (list_ptr ? *list_ptr : NULL),
		.dlist = dst,
	};

	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);
	xassert(!*list_ptr || (list_count(*list_ptr) >= 0));
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

extern int dump(void *src, ssize_t src_bytes, const parser_t *const parser,
		data_t *dst, args_t *args)
{
	int rc;
	data_t *pd;

	log_flag(DATA, "dump %zd byte %s object at 0x%" PRIxPTR " with parser %s(0x%" PRIxPTR ") to data 0x%" PRIxPTR,
		 (src_bytes == NO_VAL ? -1 : src_bytes),
		 parser->obj_type_string, (uintptr_t) src, parser->type_string,
		 (uintptr_t) parser, (uintptr_t) dst);

	check_parser(parser);
	xassert(!parser->skip);
	xassert(dst && (data_get_type(dst) != DATA_TYPE_NONE));
	xassert(args->magic == MAGIC_ARGS);
	xassert((src_bytes == NO_VAL) || (src_bytes > 0));
	xassert(src);
	/*
	 * Make sure the source object is the same size since there is no
	 * way to dump value of __typeof__ as a value in C.
	 */
	xassert((parser->size == NO_VAL) || (src_bytes == NO_VAL) ||
		(src_bytes == parser->size));

	if ((rc = load_prereqs(DUMPING, parser, args)))
		goto done;

	/* only look for child via key if there was one defined */
	if (parser->key) {
		/*
		 * Detect duplicate keys - unless parser is for an enum flag
		 * where repeats are expected.
		 */
		xassert(parser->flag ||
			!data_resolve_dict_path(dst, parser->key));
		pd = data_define_dict_path(dst, parser->key);
	} else {
		pd = dst;
	}

	xassert(pd && (data_get_type(pd) != DATA_TYPE_NONE));

	if (parser->flag != FLAG_TYPE_NONE) {
		verify_parser_sliced(parser);
		xassert((data_get_type(pd) == DATA_TYPE_NULL) ||
			(data_get_type(pd) == DATA_TYPE_LIST));
		rc = dump_flag(src, parser, pd, args);
		goto done;
	}

	if (parser->fields) {
		verify_parser_not_sliced(parser);
		xassert((data_get_type(pd) == DATA_TYPE_NULL) ||
			(data_get_type(pd) == DATA_TYPE_DICT));
		/* recursively run the parsers for each struct field */
		for (int i = 0; !rc && (i < parser->field_count); i++) {
			const parser_t *const pchild = &parser->fields[i];
			void *schild = src;

			check_parser(pchild);
			verify_parser_sliced(pchild);

			if (pchild->skip) {
				log_flag(DATA, "SKIP: %s parser %s[%d]->%s(0x%" PRIxPTR ") for %s(0x%" PRIxPTR ")->%s(+%zd) for data(0x%" PRIxPTR ")/%s(0x%" PRIxPTR ")",
					 pchild->obj_type_string, parser->type_string,
					 i, pchild->type_string, (uintptr_t) pchild,
					 parser->obj_type_string, (uintptr_t) src,
					 parser->field_name, parser->ptr_offset,
					 (uintptr_t) dst, parser->key, (uintptr_t) pd);
				continue;
			}

			if (parser->ptr_offset != NO_VAL)
				schild += parser->ptr_offset;

			log_flag(DATA, "BEGIN: dumping %s parser %s[%d]->%s(0x%" PRIxPTR ") for %s(0x%" PRIxPTR ")->%s(+%zd) for data(0x%" PRIxPTR ")/%s(0x%" PRIxPTR ")",
				 pchild->obj_type_string, parser->type_string,
				 i, pchild->type_string, (uintptr_t) pchild,
				 parser->obj_type_string, (uintptr_t) src,
				 parser->field_name, parser->ptr_offset,
				 (uintptr_t) dst, parser->key, (uintptr_t) pd);

			rc = dump(schild, NO_VAL, pchild, pd, args);

			log_flag(DATA, "END: dumping %s parser %s[%d]->%s(0x%" PRIxPTR ") for %s(0x%" PRIxPTR ")->%s(+%zd) for data(0x%" PRIxPTR ")/%s(0x%" PRIxPTR ")",
				 pchild->obj_type_string, parser->type_string,
				 i, pchild->type_string, (uintptr_t) pchild,
				 parser->obj_type_string, (uintptr_t) src,
				 parser->field_name, parser->ptr_offset,
				 (uintptr_t) dst, parser->key, (uintptr_t) pd);
		}

		goto done;
	}

	if (parser->list_type) {
		verify_parser_not_sliced(parser);
		xassert((data_get_type(pd) == DATA_TYPE_NULL) ||
			(data_get_type(pd) == DATA_TYPE_LIST));
		xassert((src_bytes == NO_VAL) || (src_bytes == sizeof(List)));
		xassert(!parser->dump);
		rc = _dump_list(parser, src, pd, args);
		goto done;
	}
	xassert(parser->dump != _dump_list);

	if (!parser->dump) {
		const parser_t *const pchild =
			find_parser_by_type(parser->type);
		void *schild = src;

		check_parser(pchild);

		if (parser->ptr_offset != NO_VAL)
			schild += parser->ptr_offset;

		log_flag(DATA, "%s: using %s parser %s(0x%" PRIxPTR ") for %s(0x%" PRIxPTR ")->%s(+%zd) for data(0x%" PRIxPTR ")/%s(0x%" PRIxPTR ")",
			 __func__, pchild->obj_type_string, pchild->type_string,
			 (uintptr_t) pchild, parser->obj_type_string,
			 (uintptr_t) src, parser->field_name,
			 parser->ptr_offset, (uintptr_t) dst, parser->key,
			 (uintptr_t) pd);

		rc = dump(schild, NO_VAL, pchild, pd, args);

		goto done;
	}

	xassert(data_get_type(pd) == DATA_TYPE_NULL);
	verify_parser_not_sliced(parser);

	/* must be a simple or complex type */
	rc = parser->dump(parser, src, pd, args);

done:
	log_flag(DATA, "dump %zd byte %s object at 0x%" PRIxPTR " with parser %s(0x%" PRIxPTR ") to data 0x%" PRIxPTR " rc[%d]=%s",
		 (src_bytes == NO_VAL ? -1 : src_bytes),
		 parser->obj_type_string, (uintptr_t) src, parser->type_string,
		 (uintptr_t) parser, (uintptr_t) dst, rc, slurm_strerror(rc));

	return rc;
}
