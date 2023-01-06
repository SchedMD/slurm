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

#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/cpu_frequency.h"
#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/net.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"
#include "src/interfaces/select.h"

#include "alloc.h"
#include "api.h"
#include "events.h"
#include "parsers.h"
#include "parsing.h"
#include "slurmdb_helpers.h"

#include "src/sinfo/sinfo.h" /* provides sinfo_data_t */

#define MAGIC_FOREACH_CSV_LIST 0x8891be2b
#define MAGIC_FOREACH_LIST 0xaefa2af3
#define MAGIC_FOREACH_LIST_FLAG 0xa1d4acd2
#define MAGIC_FOREACH_POPULATE_GLOBAL_TRES_LIST 0x31b8aad2
#define MAGIC_FOREACH_STEP 0x7e2eaef1
#define MAGIC_FOREACH_STRING_ID 0x2ea1be2b
#define MAGIC_FOREACH_STRING_ARRAY 0xaea1be2b
#define MAGIC_LIST_PER_TRES_TYPE_NCT 0xb1d8acd2

#define PARSER_ARRAY(type) _parser_array_##type
#define PARSER_FLAG_ARRAY(type) _parser_flag_array_##type
#define PARSE_FUNC(type) _parse_##type
#define DUMP_FUNC(type) _dump_##type
#define PARSE_DISABLED(type)                                                 \
	static int PARSE_FUNC(type)(const parser_t *const parser, void *src, \
				    data_t *dst, args_t *args,               \
				    data_t *parent_path)                     \
	{                                                                    \
		fatal_abort("parsing of DATA_PARSER_%s is not implemented",  \
			    XSTRINGIFY(type));                               \
	}

/*
 * Modify request for QOS will ignore an empty List. This allows slurmdbd to
 * know we want this field to be empty.
 */
#define EMPTY_QOS_ID_ENTRY "\'\'"

/* based on slurmdb_tres_rec_t but includes node and task */
typedef struct {
	uint64_t count;
	char *node;
	uint64_t task;
	uint32_t id;
	char *name;
	char *type;
} slurmdb_tres_nct_rec_t;

typedef enum {
	TRES_EXPLODE_COUNT = 1,
	TRES_EXPLODE_NODE,
	TRES_EXPLODE_TASK,
} tres_explode_type_t;

typedef struct {
	int magic; /* MAGIC_LIST_PER_TRES_TYPE_NCT */
	tres_explode_type_t type;
	slurmdb_tres_nct_rec_t *tres_nct;
	int tres_nct_count;
	hostlist_t host_list;
	args_t *args;
	const parser_t *const parser;
} foreach_list_per_tres_type_nct_t;

typedef struct {
	int magic; /* MAGIC_FOREACH_POPULATE_GLOBAL_TRES_LIST */
	slurmdb_tres_nct_rec_t *tres_nct;
	int tres_nct_count;
	int offset;
} foreach_populate_g_tres_list;

typedef struct {
	int magic; /* MAGIC_FOREACH_STRING_ID */
	const parser_t *const parser;
	data_t *ddst;
	data_t *parent_path;
	const char *caller;
	ssize_t index;
	List qos_list;
	args_t *args;
} foreach_qos_string_id_t;

typedef struct {
	int magic; /* MAGIC_FOREACH_STRING_ARRAY */
	char **array;
	int i;
	const parser_t *const parser;
	args_t *args;
} foreach_string_array_t;

#ifndef NDEBUG
static void _check_flag_bit(int8_t i, const flag_bit_t *bit)
{
	xassert(bit->magic == MAGIC_FLAG_BIT);
	xassert(bit->type > FLAG_BIT_TYPE_INVALID);
	xassert(bit->type < FLAG_BIT_TYPE_MAX);
	xassert(bit->name && bit->name[0]);
	/* mask must be set */
	xassert(bit->mask);
	xassert(bit->flag_size <= sizeof(bit->value));
	xassert(bit->flag_size > 0);
	xassert(bit->flag_name && bit->flag_name[0]);
	xassert(bit->mask_size <= sizeof(bit->value));
	xassert(bit->mask_size > 0);
	xassert(bit->mask_name && bit->mask_name[0]);

	if (bit->type == FLAG_BIT_TYPE_BIT) {
		/* atleast one bit must be set */
		xassert(bit->value);
		/* mask must include all value bits */
		xassert((bit->mask & bit->value) == bit->value);
	} else if (bit->type == FLAG_BIT_TYPE_EQUAL) {
		/* Only the first flag can be an equal
		 * type if all bits are being set:
		 * There can only be one EQUAL bit since
		 * they set all the bits or clear all of
		 * them.
		 */
		if (bit->mask == INFINITE64)
			xassert(i == 0);

		/*
		 * bit->mask must include all value bits
		 * (if there are any)
		 */
		xassert(!bit->value ||
			((bit->mask & bit->value) == bit->value));
	}
}

extern void check_parser_funcname(const parser_t *const parser,
				  const char *func_name)
{
	xassert(parser->magic == MAGIC_PARSER);

	xassert(parser->model > PARSER_MODEL_INVALID);
	xassert(parser->model < PARSER_MODEL_MAX);
	xassert(parser->size > 0);
	xassert(parser->obj_type_string && parser->obj_type_string[0]);

	if (parser->model == PARSER_MODEL_ARRAY_SKIP_FIELD) {
		/* field is only a place holder so most assert()s dont apply */
		xassert(parser->field_name && parser->field_name[0]);
		xassert(parser->type == DATA_PARSER_TYPE_INVALID);
		xassert(parser->flag == FLAG_TYPE_NONE);
		xassert(parser->flag_bit_array_count == NO_VAL8);
		xassert(parser->needs == NEED_NONE);
		xassert(!parser->field_name_overloads);
		xassert(!parser->key);
		xassert(!parser->type_string);
		xassert(!parser->required);
		xassert((parser->ptr_offset < NO_VAL) ||
			(parser->ptr_offset >= 0));
		return;
	}

	xassert(parser->type > DATA_PARSER_TYPE_INVALID);
	xassert(parser->type < DATA_PARSER_TYPE_MAX);
	xassert(parser->type_string && parser->type_string[0]);

	if (parser->model == PARSER_MODEL_FLAG_ARRAY) {
		/* parser of a specific flag field list */
		xassert(parser->flag != FLAG_TYPE_NONE);
		xassert(parser->flag > FLAG_TYPE_INVALID);
		xassert(parser->flag < FLAG_TYPE_MAX);

		if (parser->flag == FLAG_TYPE_BIT_ARRAY) {
			xassert(!parser->flag_name);
			xassert(parser->flag_bit_array);
			xassert(parser->flag_bit_array_count < NO_VAL8);

			for (int8_t i = 0; i < parser->flag_bit_array_count;
			     i++) {
				_check_flag_bit(i, &parser->flag_bit_array[i]);

				/* check for duplicate flag names */
				for (int8_t j = 0;
				     j < parser->flag_bit_array_count; j++) {
					xassert((i == j) ||
						xstrcasecmp(
							parser->flag_bit_array[i].name,
							parser->flag_bit_array[j].name));
				}
			}
		} else {
			xassert(parser->flag_name && parser->flag_name[0]);
			xassert(!parser->flag_bit_array);
			xassert(parser->flag_bit_array_count == NO_VAL8);
		}

		/* make sure this is not a list or array type */
		xassert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		xassert(!parser->list_del_func);
		xassert(!parser->list_new_func);
		xassert(!parser->fields);
		xassert(!parser->field_count);
		xassert(!parser->parse);
		xassert(!parser->dump);
		xassert(parser->ptr_offset == NO_VAL);
	} else if (parser->model == PARSER_MODEL_LIST) {
		/* parser of a List */
		xassert(parser->list_type > DATA_PARSER_TYPE_INVALID);
		xassert(parser->list_type < DATA_PARSER_TYPE_MAX);
		xassert(parser->flag == FLAG_TYPE_NONE);
		xassert(parser->flag_bit_array_count == NO_VAL8);
		xassert(!parser->fields);
		xassert(!parser->field_count);
		xassert(!parser->parse);
		xassert(!parser->dump);
		xassert(parser->size == sizeof(list_t *));
		xassert(parser->ptr_offset == NO_VAL);
	} else if (parser->model == PARSER_MODEL_ARRAY) {
		/* parser of a parser Array */
		xassert(parser->field_count > 0);

		xassert(parser->flag == FLAG_TYPE_NONE);
		xassert(parser->ptr_offset == NO_VAL);
		xassert(parser->flag_bit_array_count == NO_VAL8);
		xassert(!parser->parse);
		xassert(!parser->dump);
		xassert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		xassert(!parser->list_del_func);
		xassert(!parser->list_new_func);
		xassert(parser->fields);

		for (int i = 0; i < parser->field_count; i++) {
			/* recursively check the child parsers */
			check_parser(&parser->fields[i]);

			/*
			 * Verify each field_name is unique while ignoring
			 * complex parsers.
			 */
			if (parser->fields[i].field_name) {
				int matches = 0;

				for (int j = 0; j < parser->field_count; j++) {
					if (i == j)
						continue;

					if (!xstrcasecmp(
						parser->fields[i].field_name,
						parser->fields[j].field_name))
						matches++;
				}

				xassert(matches ==
					parser->fields[i].field_name_overloads);
			}

			/*
			 * Verify each key path is unique while ignoring skipped
			 * parsers
			 */
			if (parser->fields[i].key)
				for (int j = 0; j < parser->field_count; j++)
					xassert((i == j) ||
						xstrcasecmp(parser->fields[i]
								    .key,
							    parser->fields[j]
								    .key));
		}
	} else if (parser->model == PARSER_MODEL_ARRAY_LINK_SIMPLE_FIELD) {
		/* parser array link to a simple parser */

		/* real parser must exist */
		xassert(find_parser_by_type(parser->type));

		xassert(parser->field_name && parser->field_name[0]);
		xassert(parser->key && parser->key[0]);
		xassert(parser->flag == FLAG_TYPE_NONE);
		xassert(parser->flag_bit_array_count == NO_VAL8);
		xassert(!parser->fields);
		xassert(!parser->field_count);
		xassert(!parser->parse);
		xassert(!parser->dump);
		xassert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		xassert(!parser->list_del_func);
		xassert(!parser->list_new_func);
		xassert((parser->ptr_offset < NO_VAL) ||
			(parser->ptr_offset >= 0));
		/* linked parsers must always be the same size */
		xassert(parser->size ==
			find_parser_by_type(parser->type)->size);
	} else if (parser->model == PARSER_MODEL_ARRAY_LINK_FLAGS_FIELD) {
		/* parser array linked to flags array field */

		/* real parser must exist */
		xassert(find_parser_by_type(parser->type));
		xassert(parser->field_name && parser->field_name[0]);
		xassert(parser->key && parser->key[0]);
		xassert(parser->flag == FLAG_TYPE_BIT_ARRAY);
		xassert(!parser->flag_name);
		xassert(parser->flag_bit_array);
		xassert(parser->flag_bit_array_count > 0);
		xassert(parser->flag_bit_array_count < NO_VAL8);
		xassert(!parser->fields);
		xassert(!parser->field_count);
		xassert(!parser->parse);
		xassert(!parser->dump);
		xassert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		xassert(!parser->list_del_func);
		xassert(!parser->list_new_func);
		xassert((parser->ptr_offset < NO_VAL) ||
			(parser->ptr_offset >= 0));
		/* linked parsers must always be the same size */
		xassert(parser->size ==
			find_parser_by_type(parser->type)->size);
	} else if (parser->model == PARSER_MODEL_ARRAY_LINK_COMPLEX_FIELD) {
		/* parser array link to a complex parser */

		/* real parser must exist */
		xassert(find_parser_by_type(parser->type));

		xassert(parser->ptr_offset == NO_VAL);
		xassert(!parser->field_name);
		xassert(parser->key && parser->key[0]);
		xassert(parser->flag == FLAG_TYPE_NONE);
		xassert(parser->flag_bit_array_count == NO_VAL8);
		xassert(!parser->fields);
		xassert(!parser->field_count);
		xassert(!parser->parse);
		xassert(!parser->dump);
		xassert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		xassert(!parser->list_del_func);
		xassert(!parser->list_new_func);
		/*
		 * size for complex is the size of the struct and not the fields
		 * with no easy way to check they match here.
		 */
		xassert(parser->size > 0);
	} else if (parser->model == PARSER_MODEL_ARRAY_BOOL_FIELD) {
		xassert((parser->ptr_offset < NO_VAL) ||
			(parser->ptr_offset >= 0));
		xassert(parser->field_name && parser->field_name[0]);
		xassert(parser->key && parser->key[0]);
		xassert(parser->flag == FLAG_TYPE_BOOL);
		xassert(parser->flag_bit_array_count == NO_VAL8);
		xassert(!parser->fields);
		xassert(!parser->field_count);
		xassert(!parser->parse);
		xassert(!parser->dump);
		xassert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		xassert(!parser->list_del_func);
		xassert(!parser->list_new_func);
	} else if (parser->model == PARSER_MODEL_SIMPLE) {
		xassert(parser->ptr_offset == NO_VAL);
		xassert(!parser->key);
		xassert(!parser->field_name);
		xassert(parser->flag == FLAG_TYPE_NONE);
		xassert(parser->flag_bit_array_count == NO_VAL8);
		xassert(!parser->fields);
		xassert(!parser->field_count);
		xassert(parser->parse);
		xassert(parser->dump);
		xassert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		xassert(!parser->list_del_func);
	} else if (parser->model == PARSER_MODEL_COMPLEX) {
		xassert(parser->ptr_offset == NO_VAL);
		xassert(!parser->field_name);
		xassert(!parser->key);
		xassert(!parser->field_name);
		xassert(parser->flag == FLAG_TYPE_NONE);
		xassert(parser->flag_bit_array_count == NO_VAL8);
		xassert(!parser->fields);
		xassert(!parser->field_count);
		xassert(parser->parse);
		xassert(parser->dump);
		xassert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		xassert(!parser->list_del_func);
	} else {
		fatal_abort("invalid parser model %u", parser->model);
	}
}
#endif /* !NDEBUG */

static int PARSE_FUNC(QOS_ID)(const parser_t *const parser, void *obj,
			      data_t *src, args_t *args, data_t *parent_path)
{
	int rc;
	slurmdb_qos_rec_t *qos = NULL;
	uint32_t *qos_id = obj;

	xassert(args->magic == MAGIC_ARGS);

	if ((rc = resolve_qos(PARSING, parser, &qos, src, args, parent_path,
			      __func__, false)))
		return rc;

	*qos_id = qos->id;
	return rc;
}

static int PARSE_FUNC(QOS_NAME)(const parser_t *const parser, void *obj,
				data_t *src, args_t *args, data_t *parent_path)
{
	int rc;
	slurmdb_qos_rec_t *qos = NULL;
	char **qos_name = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (!(rc = resolve_qos(PARSING, parser, &qos, src, args, parent_path,
			       __func__, true))) {
		*qos_name = xstrdup(qos->name);
		return rc;
	}

	/*
	 * QOS names may not always be resolvable to a known QOS such as in the
	 * case of creating a new QOS which references a new QOS in the same QOS
	 * list. To ignore this chicken and the egg problem, we just blindly
	 * send the QOS name to slurmdbd if we can stringifiy it.
	 */
	if (data_get_type(src) == DATA_TYPE_DICT) {
		data_t *n = data_key_get(src, "name");

		if (n && !data_get_string_converted(n, qos_name))
			return SLURM_SUCCESS;

		rc = ESLURM_REST_FAIL_PARSING;
	} else if (!data_get_string_converted(src, qos_name))
		return SLURM_SUCCESS;

	if (rc) {
		char *name = NULL, *path = NULL;
		if (data_get_string_converted(src, &name))
			name = xstrdup_printf(
				"of type %s",
				data_type_to_string(data_get_type(src)));
		(void) data_list_join_str(&path, parent_path, "/");
		(void) on_error(PARSING, parser->type, args, rc, path, __func__,
				"Unable to resolve QOS %s", name);
		xfree(name);
		xfree(path);
	}

	return rc;
}

static int DUMP_FUNC(QOS_NAME)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	char **name = obj;

	xassert(args->magic == MAGIC_ARGS);

	(void) data_set_string(dst, *name);

	return SLURM_SUCCESS;
}

static int DUMP_FUNC(QOS_ID)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	uint32_t *qos_id = obj;
	slurmdb_qos_rec_t *qos = NULL;

	xassert(args->magic == MAGIC_ARGS);

	if (*qos_id == 0) {
		data_set_null(dst);
		return SLURM_SUCCESS;
	}

	/* find qos by id from global list */
	xassert(args->qos_list);
	if (!args->qos_list || list_is_empty(args->qos_list)) {
		/* no known QOS to search */
		return SLURM_SUCCESS;
	}

	qos = list_find_first(args->qos_list, slurmdb_find_qos_in_list, qos_id);
	if (!qos) {
		return on_error(DUMPING, parser->type, args,
				ESLURM_REST_EMPTY_RESULT,
				"list_find_first()->slurmdb_find_qos_in_list()",
				__func__, "Unable to find QOS with id#%d",
				*qos_id);
	}

	/*
	 * Client is only ever provided the QOS name and not the ID as the is a
	 * Slurm internal that no user should have to track.
	 */
	(void) data_set_string(dst, qos->name);

	return SLURM_SUCCESS;
}

static int _foreach_dump_qos_string_id(void *x, void *arg)
{
	char *string_id = x;
	foreach_qos_string_id_t *argstruct = arg;
	const parser_t *const parser = argstruct->parser;
	data_t *dst = argstruct->ddst;
	args_t *args = argstruct->args;
	data_t *dstring_id = data_set_string(data_new(), string_id);
	data_t *parent_path = data_set_list(data_new());
	slurmdb_qos_rec_t *qos = NULL;

	data_set_string_fmt(data_list_append(parent_path), "QOS[%s]",
			    string_id);

	xassert(argstruct->magic == MAGIC_FOREACH_STRING_ID);
	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_LIST);

	if (resolve_qos(DUMPING, parser, &qos, dstring_id, args, dstring_id,
			__func__, false)) {
		FREE_NULL_DATA(dstring_id);
		FREE_NULL_DATA(parent_path);
		return ESLURM_INVALID_QOS;
	}
	FREE_NULL_DATA(dstring_id);
	FREE_NULL_DATA(parent_path);

	(void) data_set_string(data_list_append(dst), qos->name);

	return SLURM_SUCCESS;
}

static int DUMP_FUNC(QOS_STRING_ID_LIST)(const parser_t *const parser,
					 void *obj, data_t *dst, args_t *args)
{
	/*
	 * QOS string ID list is special because the contents have dynamic sizes
	 * which must be accounted for while dumping and parsing
	 */
	List *qos_list_ptr = obj;
	List qos_list = *qos_list_ptr;
	foreach_qos_string_id_t argstruct = { .magic = MAGIC_FOREACH_STRING_ID,
					      .parser = parser,
					      .args = args,
					      .ddst = dst };

	if (!qos_list)
		return SLURM_SUCCESS;

	xassert(list_count(qos_list) >= 0);
	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(list_count(args->qos_list) >= 0);

	data_set_list(dst);

	if (list_for_each(qos_list, _foreach_dump_qos_string_id, &argstruct) <
	    0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static data_for_each_cmd_t _foreach_parse_qos_string_id(data_t *src, void *arg)
{
	foreach_qos_string_id_t *argstruct = arg;
	const parser_t *const parser = argstruct->parser;
	List qos_list = argstruct->qos_list;
	data_t *parent_path = argstruct->parent_path;
	args_t *args = argstruct->args;
	const char *caller = argstruct->caller;
	slurmdb_qos_rec_t *qos = NULL;
	data_t *ppath = data_copy(NULL, parent_path);
	data_t *ppath_last = data_get_list_last(ppath);
	int rc;

	if (argstruct->index < 0)
		argstruct->index = 0;

	/* Use jq style array zero based array notation */
	data_set_string_fmt(ppath_last, "%s[%zu]", data_get_string(ppath_last),
			    argstruct->index);

	if (!(rc = resolve_qos(PARSING, parser, &qos, src, args, parent_path,
			       caller, false)))
		list_append(qos_list, xstrdup_printf("%u", qos->id));

	FREE_NULL_DATA(ppath);
	return (rc ? DATA_FOR_EACH_FAIL : DATA_FOR_EACH_CONT);
}

static int PARSE_FUNC(QOS_STRING_ID_LIST)(const parser_t *const parser,
					  void *obj, data_t *src, args_t *args,
					  data_t *parent_path)
{
	List *qos_list_ptr = obj;
	foreach_qos_string_id_t argstruct = {
		.magic = MAGIC_FOREACH_STRING_ID,
		.parser = parser,
		.args = args,
		.qos_list = list_create(xfree_ptr),
		.parent_path = parent_path,
		.caller = __func__,
		.index = -1,
	};

	xassert(args->magic == MAGIC_ARGS);

	if (data_list_for_each(src, _foreach_parse_qos_string_id, &argstruct) <
	    0) {
		FREE_NULL_LIST(argstruct.qos_list);
		return ESLURM_REST_FAIL_PARSING;
	}

	*qos_list_ptr = argstruct.qos_list;
	return SLURM_SUCCESS;
}

static int PARSE_FUNC(QOS_PREEMPT_LIST)(const parser_t *const parser, void *obj,
					data_t *src, args_t *args,
					data_t *parent_path)
{
	int rc;
	slurmdb_qos_rec_t *qos = obj;

	xassert(!qos->preempt_list);

	if ((rc = PARSE(QOS_STRING_ID_LIST, qos->preempt_list, src, parent_path,
			args)))
		return rc;

	if (list_is_empty(qos->preempt_list)) {
		/*
		 * If the QOS list is empty, then we need to set this special
		 * entry to notify slurmdbd that this is explicilty empty and
		 * not a no change request
		 */
		list_append(qos->preempt_list, EMPTY_QOS_ID_ENTRY);
	}

	return SLURM_SUCCESS;
}

static int DUMP_FUNC(QOS_PREEMPT_LIST)(const parser_t *const parser, void *obj,
				       data_t *dst, args_t *args)
{
	slurmdb_qos_rec_t *qos = obj;

	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);
	xassert(args->qos_list);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(!qos->preempt_list);

	data_set_list(dst);

	if (!args->qos_list)
		return ESLURM_NOT_SUPPORTED;

	/* skip empty lists */
	if (!qos->preempt_bitstr || (bit_ffs(qos->preempt_bitstr) == -1))
		return SLURM_SUCCESS;

	/* based on get_qos_complete_str_bitstr() */
	for (int i = 1; (i < bit_size(qos->preempt_bitstr)); i++) {
		slurmdb_qos_rec_t *ptr_qos;

		if (!bit_test(qos->preempt_bitstr, i))
			continue;

		if (!(ptr_qos =
			      list_find_first(args->qos_list,
					      slurmdb_find_qos_in_list, &i))) {
			int rc;
			char *bits = bit_fmt_full(qos->preempt_bitstr);

			/*
			 * There is a race condition here where the global
			 * QOS list could have changed betwen the query of the
			 * list and the bitstrs. Just error and have the user
			 * try again if they want.
			 */
			rc = on_error(
				DUMPING, parser->type, args, ESLURM_INVALID_QOS,
				"list_find_first()->slurmdb_find_qos_in_list()",
				__func__,
				"Unable to resolve Preempt QOS (bit %u/%" PRId64 "[%s]) in QOS %s(%u)",
				i, bit_size(qos->preempt_bitstr), bits,
				qos->name, qos->id);

			xfree(bits);

			if (rc)
				return rc;
		} else {
			data_set_string(data_list_append(dst), ptr_qos->name);
		}
	}

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(ASSOC_ID)(const parser_t *const parser, void *obj,
				data_t *src, args_t *args, data_t *parent_path)
{
	int rc = SLURM_SUCCESS;
	uint32_t *associd = obj;
	slurmdb_assoc_rec_t *assoc = xmalloc(sizeof(*assoc));
	slurmdb_init_assoc_rec(assoc, false);

	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	rc = PARSE(ASSOC_SHORT, assoc, src, parent_path, args);

	if (!rc) {
		slurmdb_assoc_rec_t *match =
			list_find_first(args->assoc_list,
					(ListFindF) compare_assoc, assoc);

		if (match)
			*associd = match->id;
		else
			rc = ESLURM_REST_EMPTY_RESULT;
	}

	slurmdb_destroy_assoc_rec(assoc);

	return rc;
}

static int DUMP_FUNC(ASSOC_ID)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	uint32_t *associd = obj;
	slurmdb_assoc_rec_t *assoc = NULL;

	if (!*associd || (*associd == NO_VAL))
		return SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->assoc_list);

	if (!(assoc = list_find_first(args->assoc_list,
				      slurmdb_find_assoc_in_list, associd))) {
		return on_error(DUMPING, parser->type, args,
				ESLURM_DATA_CONV_FAILED,
				"list_find_first()->slurmdb_find_assoc_in_list()",
				__func__, "dumping association id#%u failed",
				*associd);
	}

	return DUMP(ASSOC_SHORT_PTR, assoc, dst, args);
}

static int _foreach_resolve_tres_id(void *x, void *arg)
{
	slurmdb_tres_rec_t *tres = (slurmdb_tres_rec_t *) x;
	args_t *args = arg;
	slurmdb_tres_rec_t *ftres;

	xassert(args->magic == MAGIC_ARGS);

	if (!tres->type && tres->id) {
		/* resolve type/name if only id provided */
		slurmdb_tres_rec_t *c =
			list_find_first_ro(args->tres_list,
					   slurmdb_find_tres_in_list,
					   &tres->id);

		if (c) {
			tres->type = xstrdup(c->type);
			tres->name = xstrdup(c->name);
		}
	}

	/*
	 * This may be a new TRES being created so there won't be an
	 * existing TRES to compare against.
	 */
	if (!(ftres = list_find_first_ro(args->tres_list,
					 (ListFindF) fuzzy_match_tres, tres)))
		return SLURM_SUCCESS;

	/* verify ID if possible */

	if ((tres->id > 0) && (tres->id != ftres->id))
		return ESLURM_INVALID_TRES;

	if (!tres->id)
		tres->id = ftres->id;

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(TRES_STR)(const parser_t *const parser, void *obj,
				data_t *src, args_t *args, data_t *parent_path)
{
	char **tres = obj;
	int rc;
	List tres_list = NULL;

	xassert(!*tres);
	xassert(args->magic == MAGIC_ARGS);

	if (!args->tres_list) {
		/* should not happen */
		xassert(args->tres_list);
		rc = ESLURM_NOT_SUPPORTED;
		goto cleanup;
	}

	if (data_get_type(src) != DATA_TYPE_LIST) {
		char *path = NULL;
		(void) data_list_join_str(&path, parent_path, "/");
		rc = on_error(PARSING, parser->type, args,
			      ESLURM_REST_FAIL_PARSING, path, __func__,
			      "TRES should be LIST but is type %s",
			      data_type_to_string(data_get_type(src)));
		xfree(path);
		goto cleanup;
	}

	if ((rc = PARSE(TRES_LIST, tres_list, src, parent_path, args)))
		goto cleanup;

	(void) list_for_each(tres_list, _foreach_resolve_tres_id, &args);

	if ((*tres = slurmdb_make_tres_string(tres_list,
					      TRES_STR_FLAG_SIMPLE))) {
		rc = SLURM_SUCCESS;
	} else {
		char *path = NULL;
		xassert(false); /* should not have failed */
		(void) data_list_join_str(&path, parent_path, "/");
		rc = on_error(PARSING, parser->type, args,
			      ESLURM_REST_FAIL_PARSING, path, __func__,
			      "Unable to convert TRES to string");
		xfree(path);
	}

cleanup:
	FREE_NULL_LIST(tres_list);
	return rc;
}

static int DUMP_FUNC(TRES_STR)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	int rc;
	char **tres = obj;
	List tres_list = NULL;

	xassert(args->magic == MAGIC_ARGS);
	xassert(args->tres_list && (list_count(args->tres_list) >= 0));

	if (!args->tres_list) {
		xassert(false);
		return on_error(DUMPING, parser->type, args,
				ESLURM_NOT_SUPPORTED, "TRES list not available",
				__func__, "TRES conversion requires TRES list");
	}

	if (!*tres || !*tres[0])
		/* ignore empty TRES strings */
		return SLURM_SUCCESS;

	slurmdb_tres_list_from_string(&tres_list, *tres, TRES_STR_FLAG_BYTES);

	if (!tres_list) {
		rc = on_error(DUMPING, parser->type, args,
			      ESLURM_DATA_CONV_FAILED,
			      "slurmdb_tres_list_from_string", __func__,
			      "Unable to convert TRES from string");
	}

	if (list_is_empty(tres_list))
		goto cleanup;

	list_for_each(tres_list, _foreach_resolve_tres_id, args);

	if ((rc = DUMP(TRES_LIST, tres_list, dst, args)))
		return rc;
cleanup:
	FREE_NULL_LIST(tres_list);

	return SLURM_SUCCESS;
}

static int _foreach_list_per_tres_type_nct(void *x, void *arg)
{
	slurmdb_tres_rec_t *tres = (slurmdb_tres_rec_t *) x;
	foreach_list_per_tres_type_nct_t *args = arg;
	slurmdb_tres_nct_rec_t *tres_nct = NULL;

	xassert(args->magic == MAGIC_LIST_PER_TRES_TYPE_NCT);

	for (int i = 0; i < args->tres_nct_count; i++)
		if (args->tres_nct[i].id == tres->id)
			tres_nct = args->tres_nct + i;

	xassert(tres_nct);
	if (!tres_nct)
		/* out of sync?? */
		return -1;

	switch (args->parser->type) {
	case TRES_EXPLODE_NODE :
		xassert(!tres_nct->node);
		free(tres_nct->node);
		/* based on find_hostname() */
		tres_nct->node = hostlist_nth(args->host_list, tres->count);
		return 1;
	case TRES_EXPLODE_TASK :
		xassert(!tres_nct->task);
		tres_nct->task = tres->count;
		return 1;
	case TRES_EXPLODE_COUNT :
		xassert(!tres_nct->count);
		tres_nct->count = tres->count;
		return 1;
	default :
		fatal("%s: unexpected type", __func__);
	}
}

static int _foreach_populate_g_tres_list(void *x, void *arg)
{
	slurmdb_tres_rec_t *tres = x;
	foreach_populate_g_tres_list *args = arg;
	slurmdb_tres_nct_rec_t *tres_nct = args->tres_nct + args->offset;

	xassert(args->magic == MAGIC_FOREACH_POPULATE_GLOBAL_TRES_LIST);

	tres_nct->id = tres->id;
	tres_nct->name = tres->name;
	tres_nct->type = tres->type;

	xassert(args->offset < args->tres_nct_count);
	args->offset += 1;
	return 0;
}

static int _dump_tres_nct(const parser_t *const parser, data_t *dst,
			  char *tres_count, char *tres_node, char *tres_task,
			  char *nodes, args_t *args)
{
	int rc = SLURM_SUCCESS;
	foreach_list_per_tres_type_nct_t fargs = {
		.magic = MAGIC_LIST_PER_TRES_TYPE_NCT,
		.args = args,
		.parser = parser,
	};
	foreach_populate_g_tres_list gtres_args = {
		.magic = MAGIC_FOREACH_POPULATE_GLOBAL_TRES_LIST,
	};
	slurmdb_tres_nct_rec_t *tres_nct = NULL;
	int tres_nct_count = 0;
	List tres_count_list = NULL;
	List tres_node_list = NULL;
	List tres_task_list = NULL;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	data_set_list(dst);

	xassert(args->tres_list);
	if (!args->tres_list)
		goto cleanup;

	if (!tres_count && !tres_node && !tres_task)
		/* ignore empty TRES strings */
		goto cleanup;

	fargs.tres_nct_count = gtres_args.tres_nct_count = tres_nct_count =
		list_count(args->tres_list);
	fargs.tres_nct = gtres_args.tres_nct = tres_nct =
		xcalloc(list_count(args->tres_list), sizeof(*tres_nct));
	if (list_for_each_ro(args->tres_list, _foreach_populate_g_tres_list,
			     &gtres_args) < 0)
		goto cleanup;

	fargs.host_list = hostlist_create(nodes);

	slurmdb_tres_list_from_string(&tres_count_list, tres_count,
				      TRES_STR_FLAG_BYTES);
	slurmdb_tres_list_from_string(&tres_node_list, tres_node,
				      TRES_STR_FLAG_BYTES);
	slurmdb_tres_list_from_string(&tres_task_list, tres_task,
				      TRES_STR_FLAG_BYTES);

	fargs.type = TRES_EXPLODE_COUNT;
	if (tres_count_list &&
	    (list_for_each(tres_count_list, _foreach_list_per_tres_type_nct,
			   &fargs) < 0))
		goto cleanup;
	fargs.type = TRES_EXPLODE_NODE;
	if (tres_node_list &&
	    (list_for_each(tres_node_list, _foreach_list_per_tres_type_nct,
			   &fargs) < 0))
		goto cleanup;
	fargs.type = TRES_EXPLODE_TASK;
	if (tres_task_list &&
	    (list_for_each(tres_task_list, _foreach_list_per_tres_type_nct,
			   &fargs) < 0))
		goto cleanup;
	/* clear type to catch unintended reuse */
	fargs.type = 0;

	for (int i = 0; !rc && (i < tres_nct_count); i++)
		if (tres_nct[i].count || tres_nct[i].node || tres_nct[i].task)
			rc = DUMP(TRES_NCT, tres_nct[i],
				  data_set_dict(data_list_append(dst)), args);

cleanup:
	FREE_NULL_LIST(tres_count_list);
	FREE_NULL_LIST(tres_node_list);
	FREE_NULL_LIST(tres_task_list);
	FREE_NULL_HOSTLIST(fargs.host_list);
	for (int i = 0; i < tres_nct_count; i++)
		/* hostlist_nth doesn't use xfree() */
		free(tres_nct[i].node);
	xfree(tres_nct);

	return rc;
}

static int PARSE_FUNC(ADMIN_LVL)(const parser_t *const parser, void *obj,
				 data_t *src, args_t *args, data_t *parent_path)
{
	uint16_t *admin_level = obj;

	if (data_convert_type(src, DATA_TYPE_STRING) != DATA_TYPE_STRING) {
		char *path = NULL;
		int rc;
		(void) data_list_join_str(&path, parent_path, "/");
		rc = on_error(
			PARSING, parser->type, args, ESLURM_REST_FAIL_PARSING,
			path, __func__,
			"unable to convert administrator level to string from type %s",
			data_type_to_string(data_get_type(src)));
		xfree(path);
		return rc;
	}

	xassert(args->magic == MAGIC_ARGS);

	*admin_level = str_2_slurmdb_admin_level(data_get_string(src));

	if (*admin_level == SLURMDB_ADMIN_NOTSET) {
		char *path = NULL;
		int rc;
		(void) data_list_join_str(&path, parent_path, "/");
		rc = on_error(
			PARSING, parser->type, args, ESLURM_REST_FAIL_PARSING,
			path, __func__,
			"unable to parse %s as a known administrator level",
			data_get_string(src));
		xfree(path);
		return rc;
	}

	return SLURM_SUCCESS;
}

static int DUMP_FUNC(ADMIN_LVL)(const parser_t *const parser, void *obj,
				data_t *dst, args_t *args)
{
	uint16_t *admin_level = obj;

	xassert(args->magic == MAGIC_ARGS);

	(void) data_set_string(dst, slurmdb_admin_level_str(*admin_level));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_EXIT_CODE)

static int DUMP_FUNC(JOB_EXIT_CODE)(const parser_t *const parser, void *obj,
				    data_t *dst, args_t *args)
{
	uint32_t *ec = obj;
	data_t *drc, *dsc;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	(void) data_set_dict(dst);

	dsc = data_key_set(dst, "status");
	drc = data_key_set(dst, "return_code");

	if (*ec == NO_VAL)
		data_set_string(dsc, "PENDING");
	else if (WIFEXITED(*ec)) {
		data_set_string(dsc, "SUCCESS");
		data_set_int(drc, 0);
	} else if (WIFSIGNALED(*ec)) {
		data_t *sig = data_set_dict(data_key_set(dst, "signal"));
		data_set_string(dsc, "SIGNALED");

		data_set_int(data_key_set(sig, "signal_id"), WTERMSIG(*ec));
		data_set_string(data_key_set(sig, "name"),
				strsignal(WTERMSIG(*ec)));
	} else if (WCOREDUMP(*ec)) {
		data_set_string(dsc, "CORE_DUMPED");
	} else {
		data_set_string(dsc, "ERROR");
		data_set_int(drc, WEXITSTATUS(*ec));
	}

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_USER)

static int DUMP_FUNC(JOB_USER)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	char *user;
	slurmdb_job_rec_t *job = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	/* job user may be set but fail back to resolving the uid */

	if (job->user && job->user[0]) {
		data_set_string(dst, job->user);
		return SLURM_SUCCESS;
	}

	user = uid_to_string_or_null(job->uid);

	if (user && user[0]) {
		data_set_string_own(dst, user);
		return SLURM_SUCCESS;
	}

	data_set_null(dst);
	xfree(user);
	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_REC_ARRAY_PTR)

static int DUMP_FUNC(STATS_REC_ARRAY_PTR)(const parser_t *const parser,
					  void *obj, data_t *dst, args_t *args)
{
	slurmdb_rollup_stats_t **ptr = obj;

	if (!*ptr)
		return SLURM_SUCCESS;

	return DUMP(STATS_REC_ARRAY, **ptr, dst, args);
}

PARSE_DISABLED(STATS_REC_ARRAY)

static int DUMP_FUNC(STATS_REC_ARRAY)(const parser_t *const parser, void *obj,
				      data_t *dst, args_t *args)
{
	slurmdb_rollup_stats_t *rollup_stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	data_set_list(dst);

	if (!rollup_stats) {
		return on_error(DUMPING, parser->type, args,
				ESLURM_DATA_CONV_FAILED, "slurmctld", __func__,
				"rollup stats not provided by controller");
	}

	for (int i = 0; i < DBD_ROLLUP_COUNT; i++) {
		data_t *d;
		uint64_t roll_ave;

		if (rollup_stats->time_total[i] == 0)
			continue;

		d = data_set_dict(data_list_append(dst));

		if (i == 0)
			data_set_string(data_key_set(d, "type"), "internal");
		else if (i == 1)
			data_set_string(data_key_set(d, "type"), "user");
		else
			data_set_string(data_key_set(d, "type"), "unknown");

		data_set_int(data_key_set(d, "last_run"),
			     rollup_stats->timestamp[i]);

		roll_ave = rollup_stats->time_total[i];
		if (rollup_stats->count[i] > 1)
			roll_ave /= rollup_stats->count[i];

		data_set_int(data_key_set(d, "last_cycle"),
			     rollup_stats->time_last[i]);
		data_set_int(data_key_set(d, "max_cycle"),
			     rollup_stats->time_max[i]);
		data_set_int(data_key_set(d, "total_time"),
			     rollup_stats->time_total[i]);
		data_set_int(data_key_set(d, "total_cycles"),
			     rollup_stats->count[i]);
		data_set_int(data_key_set(d, "mean_cycles"), roll_ave);
	}

	return SLURM_SUCCESS;
}

PARSE_DISABLED(RPC_ID)

static int DUMP_FUNC(RPC_ID)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	slurmdbd_msg_type_t *id = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_string(dst, slurmdbd_msg_type_2_str(*id, 1));

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(SELECT_PLUGIN_ID)(const parser_t *const parser, void *obj,
					data_t *src, args_t *args,
					data_t *parent_path)
{
	int *id = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) == DATA_TYPE_NULL)
		return ESLURM_REST_FAIL_PARSING;
	else if (data_convert_type(src, DATA_TYPE_STRING) == DATA_TYPE_STRING &&
		 (*id = select_string_to_plugin_id(data_get_string(src)) > 0))
		return SLURM_SUCCESS;

	return ESLURM_REST_FAIL_PARSING;
}

static int DUMP_FUNC(SELECT_PLUGIN_ID)(const parser_t *const parser, void *obj,
				       data_t *dst, args_t *args)
{
	int *id = obj;
	char *s = select_plugin_id_to_string(*id);

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (s) {
		data_set_string(dst, s);
	} else
		data_set_null(dst);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(TASK_DISTRIBUTION)

static int DUMP_FUNC(TASK_DISTRIBUTION)(const parser_t *const parser, void *obj,
					data_t *dst, args_t *args)
{
	uint32_t *dist = obj;
	char *d = slurm_step_layout_type_name(*dist);

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_string_own(dst, d);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(SLURM_STEP_ID)

static int DUMP_FUNC(SLURM_STEP_ID)(const parser_t *const parser, void *obj,
				    data_t *dst, args_t *args)
{
	int rc = SLURM_SUCCESS;
	slurm_step_id_t *id = obj;

	xassert(args->magic == MAGIC_ARGS);

	data_set_dict(dst);

	if (id->job_id != NO_VAL)
		data_set_int(data_key_set(dst, "job_id"), id->job_id);
	if (id->step_het_comp != NO_VAL)
		data_set_int(data_key_set(dst, "step_het_component"),
			     id->step_het_comp);
	if (id->step_id != NO_VAL)
		rc = DUMP(STEP_ID, id->step_id, data_key_set(dst, "step_id"),
			  args);

	return rc;
}

PARSE_DISABLED(STEP_ID)

static int DUMP_FUNC(STEP_ID)(const parser_t *const parser, void *obj,
			      data_t *dst, args_t *args)
{
	uint32_t *id = obj;

	xassert(args->magic == MAGIC_ARGS);

	// TODO rewrite after bug#9622 resolved

	switch (*id) {
	case SLURM_EXTERN_CONT :
		data_set_string(dst, "extern");
		break;
	case SLURM_BATCH_SCRIPT :
		data_set_string(dst, "batch");
		break;
	case SLURM_PENDING_STEP :
		data_set_string(dst, "pending");
		break;
	case SLURM_INTERACTIVE_STEP :
		data_set_string(dst, "interactive");
		break;
	default :
		data_set_int(dst, *id);
	}

	return SLURM_SUCCESS;
}

PARSE_DISABLED(WCKEY_TAG)

static int DUMP_FUNC(WCKEY_TAG)(const parser_t *const parser, void *obj,
				data_t *dst, args_t *args)
{
	char **src = obj;
	data_t *flags, *key;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!*src) {
		data_set_null(dst);
		return SLURM_SUCCESS;
	}

	key = data_key_set(data_set_dict(dst), "wckey");
	flags = data_set_list(data_key_set(dst, "flags"));

	if (*src[0] == '*') {
		data_set_string(data_list_append(flags), "ASSIGNED_DEFAULT");
		data_set_string(key, (*src + 1));
	} else
		data_set_string(key, *src);

	return SLURM_SUCCESS;
}

static int DUMP_FUNC(USER_ID)(const parser_t *const parser, void *obj,
			      data_t *dst, args_t *args)
{
	uid_t *uid = obj;
	char *u;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((u = uid_to_string_or_null(*uid)))
		data_set_string_own(dst, u);
	else
		data_set_null(dst);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(USER_ID)(const parser_t *const parser, void *obj,
			       data_t *src, args_t *args, data_t *parent_path)
{
	uid_t *uid = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) == DATA_TYPE_NULL)
		return ESLURM_REST_FAIL_PARSING;
	else if (data_convert_type(src, DATA_TYPE_STRING) == DATA_TYPE_STRING &&
		 !uid_from_string(data_get_string(src), uid))
		return SLURM_SUCCESS;

	return ESLURM_REST_FAIL_PARSING;
}

PARSE_DISABLED(GROUP_ID)

static int DUMP_FUNC(GROUP_ID)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	gid_t *gid = obj;
	char *g;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((g = gid_to_string_or_null(*gid)))
		data_set_string_own(dst, g);
	else
		data_set_null(dst);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_REASON)

static int DUMP_FUNC(JOB_REASON)(const parser_t *const parser, void *obj,
				 data_t *dst, args_t *args)
{
	uint32_t *state = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_string(dst, job_reason_string(*state));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_STATE)

static int DUMP_FUNC(JOB_STATE)(const parser_t *const parser, void *obj,
				data_t *dst, args_t *args)
{
	uint32_t *state = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_string(dst, job_state_string(*state));

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(STRING)(const parser_t *const parser, void *obj,
			      data_t *str, args_t *args, data_t *parent_path)
{
	int rc = SLURM_SUCCESS;
	char **dst = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL) {
		xfree(*dst);
	} else if (data_convert_type(str, DATA_TYPE_STRING) ==
		   DATA_TYPE_STRING) {
		xfree(*dst);
		*dst = xstrdup(data_get_string(str));
	} else {
		rc = ESLURM_DATA_CONV_FAILED;
	}

	debug5("%s: string %s rc[%d]=%s", __func__, *dst, rc,
	       slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(STRING)(const parser_t *const parser, void *obj,
			     data_t *data, args_t *args)
{
	int rc = SLURM_SUCCESS;
	char **src = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (*src)
		data_set_string(data, *src);
	else
		data_set_null(data);

	return rc;
}

static int PARSE_FUNC(FLOAT128)(const parser_t *const parser, void *obj,
				data_t *str, args_t *args, data_t *parent_path)
{
	long double *dst = obj;
	int rc = SLURM_SUCCESS;

	xassert(sizeof(long double) * 8 == 128);
	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = (double) NO_VAL;
	else if (data_convert_type(str, DATA_TYPE_FLOAT) == DATA_TYPE_FLOAT)
		*dst = data_get_float(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %Lf rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(FLOAT128)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	long double *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	/* see bug#9674 */
	if (((uint32_t) *src == INFINITE) || ((uint32_t) *src == NO_VAL))
		data_set_null(dst);
	else
		(void) data_set_float(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(FLOAT64)(const parser_t *const parser, void *obj,
			       data_t *str, args_t *args, data_t *parent_path)
{
	double *dst = obj;
	int rc = SLURM_SUCCESS;

	xassert(sizeof(double) * 8 == 64);
	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = 0;
	else if (data_convert_type(str, DATA_TYPE_FLOAT) == DATA_TYPE_FLOAT)
		*dst = data_get_float(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %f rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(FLOAT64)(const parser_t *const parser, void *obj,
			      data_t *dst, args_t *args)
{
	double *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	(void) data_set_float(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(FLOAT64_NO_VAL)(const parser_t *const parser, void *obj,
				      data_t *str, args_t *args,
				      data_t *parent_path)
{
	double *dst = obj;

	xassert(sizeof(double) * 8 == 64);
	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL) {
		*dst = (double) NO_VAL;
		return SLURM_SUCCESS;
	}

	return PARSE_FUNC(FLOAT64)(parser, obj, str, args, parent_path);
}

static int DUMP_FUNC(FLOAT64_NO_VAL)(const parser_t *const parser, void *obj,
				     data_t *dst, args_t *args)
{
	double *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	/* see bug#9674 about double comparison and casting */
	if (((uint32_t) *src == INFINITE) || ((uint32_t) *src == NO_VAL))
		(void) data_set_null(dst);
	else
		(void) data_set_float(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(INT64)(const parser_t *const parser, void *obj,
			     data_t *str, args_t *args, data_t *parent_path)
{
	int64_t *dst = obj;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = (double) NO_VAL;
	else if (data_convert_type(str, DATA_TYPE_FLOAT) == DATA_TYPE_FLOAT)
		*dst = data_get_float(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %" PRId64 " rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(INT64)(const parser_t *const parser, void *obj,
			    data_t *dst, args_t *args)
{
	int64_t *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	/* Never set values of INF or NO_VAL */
	if ((*src == NO_VAL64) || (*src == INFINITE64))
		(void) data_set_null(dst);
	else
		(void) data_set_int(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(UINT16)(const parser_t *const parser, void *obj,
			      data_t *str, args_t *args, data_t *parent_path)
{
	uint16_t *dst = obj;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = 0;
	else if (data_convert_type(str, DATA_TYPE_INT_64) == DATA_TYPE_INT_64)
		*dst = data_get_int(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %hu rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(UINT16)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	uint16_t *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	(void) data_set_int(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(UINT16_NO_VAL)(const parser_t *const parser, void *obj,
				     data_t *str, args_t *args,
				     data_t *parent_path)
{
	uint16_t *dst = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL) {
		*dst = NO_VAL16;
		return SLURM_SUCCESS;
	}

	return PARSE_FUNC(UINT16)(parser, obj, str, args, parent_path);
}

static int DUMP_FUNC(UINT16_NO_VAL)(const parser_t *const parser, void *obj,
				    data_t *dst, args_t *args)
{
	uint16_t *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	/* Never set values of INF or NO_VAL */
	if ((*src == NO_VAL16) || (*src == INFINITE16))
		data_set_null(dst);
	else
		(void) data_set_int(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(UINT64)(const parser_t *const parser, void *obj,
			      data_t *str, args_t *args, data_t *parent_path)
{
	uint64_t *dst = obj;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = 0;
	else if (data_convert_type(str, DATA_TYPE_INT_64) == DATA_TYPE_INT_64)
		*dst = data_get_int(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %" PRIu64 " rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(UINT64)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	uint64_t *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	/* Never set values of INF or NO_VAL */
	if ((*src == NO_VAL64) || (*src == INFINITE64))
		data_set_null(dst);
	else
		(void) data_set_int(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(UINT32)(const parser_t *const parser, void *obj,
			      data_t *str, args_t *args, data_t *parent_path)
{
	uint32_t *dst = obj;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL) {
		*dst = 0;
	} else if (data_convert_type(str, DATA_TYPE_INT_64) ==
		   DATA_TYPE_INT_64) {
		/* catch -1 and set to NO_VAL instead of rolling */
		if (0xFFFFFFFF00000000 & data_get_int(str))
			*dst = NO_VAL;
		else
			*dst = data_get_int(str);
	} else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %u rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(UINT32)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	uint32_t *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	log_flag(DATA, "%s: uint32_t 0x%" PRIxPTR "=%u", __func__,
		 (uintptr_t) src, *src);
	(void) data_set_int(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(UINT32_NO_VAL)(const parser_t *const parser, void *obj,
				     data_t *str, args_t *args,
				     data_t *parent_path)
{
	uint32_t *dst = obj;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = NO_VAL;
	else
		rc = PARSE_FUNC(UINT32)(parser, obj, str, args, parent_path);

	log_flag(DATA, "%s: string %u rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(UINT32_NO_VAL)(const parser_t *const parser, void *obj,
				    data_t *dst, args_t *args)
{
	int rc = SLURM_SUCCESS;
	uint32_t *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	if ((*src == NO_VAL) || (*src == INFINITE))
		data_set_null(dst);
	else
		rc = DUMP_FUNC(UINT32)(parser, obj, dst, args);

	return rc;
}

PARSE_DISABLED(STEP_NODES)

static int DUMP_FUNC(STEP_NODES)(const parser_t *const parser, void *src,
				 data_t *dst, args_t *args)
{
	int rc;
	slurmdb_step_rec_t *step = src;
	hostlist_t host_list;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	/* ignore empty node list */
	if (!step->nodes)
		return SLURM_SUCCESS;

	if (!(host_list = hostlist_create(step->nodes)))
		return errno;

	xassert(hostlist_count(host_list) == step->nnodes);

	rc = DUMP(HOSTLIST, host_list, dst, args);

	FREE_NULL_HOSTLIST(host_list);
	return rc;
}

PARSE_DISABLED(STEP_TRES_REQ_MAX)

static int DUMP_FUNC(STEP_TRES_REQ_MAX)(const parser_t *const parser, void *src,
					data_t *dst, args_t *args)
{
	slurmdb_step_rec_t *step = src;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	return _dump_tres_nct(parser, dst, step->stats.tres_usage_in_max,
			      step->stats.tres_usage_in_max_nodeid,
			      step->stats.tres_usage_in_max_taskid, step->nodes,
			      args);
}

PARSE_DISABLED(STEP_TRES_REQ_MIN)

static int DUMP_FUNC(STEP_TRES_REQ_MIN)(const parser_t *const parser, void *src,
					data_t *dst, args_t *args)
{
	slurmdb_step_rec_t *step = src;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	return _dump_tres_nct(parser, dst, step->stats.tres_usage_in_min,
			      step->stats.tres_usage_in_min_nodeid,
			      step->stats.tres_usage_in_min_taskid, step->nodes,
			      args);
}

PARSE_DISABLED(STEP_TRES_USAGE_MAX)

static int DUMP_FUNC(STEP_TRES_USAGE_MAX)(const parser_t *const parser,
					  void *src, data_t *dst, args_t *args)
{
	slurmdb_step_rec_t *step = src;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	return _dump_tres_nct(parser, dst, step->stats.tres_usage_out_max,
			      step->stats.tres_usage_out_max_nodeid,
			      step->stats.tres_usage_out_max_taskid,
			      step->nodes, args);
}

PARSE_DISABLED(STEP_TRES_USAGE_MIN)

static int DUMP_FUNC(STEP_TRES_USAGE_MIN)(const parser_t *const parser,
					  void *src, data_t *dst, args_t *args)
{
	slurmdb_step_rec_t *step = src;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	return _dump_tres_nct(parser, dst, step->stats.tres_usage_out_min,
			      step->stats.tres_usage_out_min_nodeid,
			      step->stats.tres_usage_out_min_taskid,
			      step->nodes, args);
}

static int PARSE_FUNC(BOOL)(const parser_t *const parser, void *obj,
			    data_t *src, args_t *args, data_t *parent_path)
{
	uint8_t *b = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_convert_type(src, DATA_TYPE_BOOL) == DATA_TYPE_BOOL) {
		*b = data_get_bool(src);
		return SLURM_SUCCESS;
	}

	return ESLURM_REST_FAIL_PARSING;
}

static int DUMP_FUNC(BOOL)(const parser_t *const parser, void *obj, data_t *dst,
			   args_t *args)
{
	uint8_t *b = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_bool(dst, *b);
	return SLURM_SUCCESS;
}

static int PARSE_FUNC(BOOL16)(const parser_t *const parser, void *obj,
			      data_t *src, args_t *args, data_t *parent_path)
{
	uint16_t *b = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_convert_type(src, DATA_TYPE_BOOL) == DATA_TYPE_BOOL) {
		*b = data_get_bool(src);
		return SLURM_SUCCESS;
	}

	return ESLURM_REST_FAIL_PARSING;
}

static int DUMP_FUNC(BOOL16)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	uint16_t *b = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_bool(dst, *b);
	return SLURM_SUCCESS;
}

static int PARSE_FUNC(BOOL16_NO_VAL)(const parser_t *const parser, void *obj,
				     data_t *src, args_t *args,
				     data_t *parent_path)
{
	uint16_t *b = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) == DATA_TYPE_NULL) {
		*b = NO_VAL16;
		return SLURM_SUCCESS;
	}

	return PARSE_FUNC(BOOL16)(parser, obj, src, args, parent_path);
}

static int DUMP_FUNC(BOOL16_NO_VAL)(const parser_t *const parser, void *obj,
				    data_t *dst, args_t *args)
{
	uint16_t *b = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (*b == NO_VAL16) {
		/* leave as NULL */
		return SLURM_SUCCESS;
	}

	data_set_bool(dst, *b);
	return SLURM_SUCCESS;
}

static int PARSE_FUNC(ASSOC_SHORT_PTR)(const parser_t *const parser, void *obj,
				       data_t *src, args_t *args,
				       data_t *parent_path)
{
	slurmdb_assoc_rec_t **assoc_ptr = obj;
	slurmdb_assoc_rec_t *assoc = NULL;
	int rc;

	xassert(assoc_ptr);
	xassert(!*assoc_ptr);
	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) != DATA_TYPE_DICT)
		return ESLURM_REST_FAIL_PARSING;

	if ((rc = PARSE(ASSOC_SHORT, assoc, src, parent_path, args))) {
		slurmdb_destroy_assoc_rec(assoc);
		assoc = NULL;
	} else {
		*assoc_ptr = assoc;
	}

	return rc;
}

static int DUMP_FUNC(ASSOC_SHORT_PTR)(const parser_t *const parser, void *obj,
				      data_t *dst, args_t *args)
{
	slurmdb_assoc_rec_t **assoc_ptr = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(assoc_ptr);

	if (!*assoc_ptr) {
		/* ignore NULL assoc ptr */
		return SLURM_SUCCESS;
	}

	return DUMP(ASSOC_SHORT, **assoc_ptr, dst, args);
}

static int PARSE_FUNC(ASSOC_USAGE_PTR)(const parser_t *const parser, void *obj,
				       data_t *src, args_t *args,
				       data_t *parent_path)
{
	slurmdb_assoc_usage_t **assoc_ptr = obj;
	slurmdb_assoc_usage_t *assoc = NULL;
	int rc;

	xassert(assoc_ptr);
	xassert(!*assoc_ptr);
	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) != DATA_TYPE_DICT)
		return ESLURM_REST_FAIL_PARSING;

	if ((rc = PARSE(ASSOC_USAGE, assoc, src, parent_path, args))) {
		slurmdb_destroy_assoc_usage(assoc);
		assoc = NULL;
	} else {
		*assoc_ptr = assoc;
	}

	return rc;
}

static int DUMP_FUNC(ASSOC_USAGE_PTR)(const parser_t *const parser, void *obj,
				      data_t *dst, args_t *args)
{
	slurmdb_assoc_usage_t **assoc_ptr = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(assoc_ptr);

	if (!*assoc_ptr) {
		/* ignore NULL assoc ptr */
		return SLURM_SUCCESS;
	}

	return DUMP(ASSOC_USAGE, *assoc_ptr, dst, args);
}

PARSE_DISABLED(STATS_MSG_CYCLE_MEAN)

static int DUMP_FUNC(STATS_MSG_CYCLE_MEAN)(const parser_t *const parser,
					   void *obj, data_t *dst, args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->schedule_cycle_counter)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->schedule_cycle_sum /
			   stats->schedule_cycle_counter));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_CYCLE_MEAN_DEPTH)

static int DUMP_FUNC(STATS_MSG_CYCLE_MEAN_DEPTH)(const parser_t *const parser,
						 void *obj, data_t *dst,
						 args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->schedule_cycle_counter)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->schedule_cycle_depth /
			   stats->schedule_cycle_counter));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_CYCLE_PER_MIN)

static int DUMP_FUNC(STATS_MSG_CYCLE_PER_MIN)(const parser_t *const parser,
					      void *obj, data_t *dst,
					      args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((stats->req_time - stats->req_time_start) < 60)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->schedule_cycle_counter /
			   ((stats->req_time - stats->req_time_start) / 60)));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_BF_CYCLE_MEAN)

static int DUMP_FUNC(STATS_MSG_BF_CYCLE_MEAN)(const parser_t *const parser,
					      void *obj, data_t *dst,
					      args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->bf_cycle_counter)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->bf_cycle_sum / stats->bf_cycle_counter));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_BF_DEPTH_MEAN)

static int DUMP_FUNC(STATS_MSG_BF_DEPTH_MEAN)(const parser_t *const parser,
					      void *obj, data_t *dst,
					      args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->bf_cycle_counter)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->bf_depth_sum / stats->bf_cycle_counter));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_BF_DEPTH_MEAN_TRY)

static int DUMP_FUNC(STATS_MSG_BF_DEPTH_MEAN_TRY)(const parser_t *const parser,
						  void *obj, data_t *dst,
						  args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->bf_cycle_counter)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->bf_depth_try_sum / stats->bf_cycle_counter));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_BF_QUEUE_LEN_MEAN)

static int DUMP_FUNC(STATS_MSG_BF_QUEUE_LEN_MEAN)(const parser_t *const parser,
						  void *obj, data_t *dst,
						  args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->bf_cycle_counter)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->bf_queue_len_sum / stats->bf_cycle_counter));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_BF_TABLE_SIZE_MEAN)

static int DUMP_FUNC(STATS_MSG_BF_TABLE_SIZE_MEAN)(const parser_t *const parser,
						   void *obj, data_t *dst,
						   args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->bf_cycle_counter)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->bf_table_size_sum / stats->bf_cycle_counter));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_BF_ACTIVE)

static int DUMP_FUNC(STATS_MSG_BF_ACTIVE)(const parser_t *const parser,
					  void *obj, data_t *dst, args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_bool(dst, (stats->bf_active != 0));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_RPCS_BY_TYPE)

static int DUMP_FUNC(STATS_MSG_RPCS_BY_TYPE)(const parser_t *const parser,
					     void *obj, data_t *dst,
					     args_t *args)
{
	uint32_t *rpc_type_ave_time;
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->rpc_type_size)
		return SLURM_SUCCESS;

	data_set_list(dst);

	rpc_type_ave_time =
		xcalloc(stats->rpc_type_size, sizeof(*rpc_type_ave_time));

	for (int i = 0; i < stats->rpc_type_size; i++) {
		if ((stats->rpc_type_time[i] > 0) &&
		    (stats->rpc_type_cnt[i] > 0))
			rpc_type_ave_time[i] = stats->rpc_type_time[i] /
					       stats->rpc_type_cnt[i];
		else
			rpc_type_ave_time[i] = 0;
	}

	for (int i = 0; i < stats->rpc_type_size; i++) {
		data_t *r = data_set_dict(data_list_append(dst));
		data_set_string(data_key_set(r, "message_type"),
				rpc_num2string(stats->rpc_type_id[i]));
		data_set_int(data_key_set(r, "type_id"), stats->rpc_type_id[i]);
		data_set_int(data_key_set(r, "count"), stats->rpc_type_cnt[i]);
		data_set_int(data_key_set(r, "average_time"),
			     rpc_type_ave_time[i]);
		data_set_int(data_key_set(r, "total_time"),
			     stats->rpc_type_time[i]);
	}

	xfree(rpc_type_ave_time);
	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_RPCS_BY_USER)

static int DUMP_FUNC(STATS_MSG_RPCS_BY_USER)(const parser_t *const parser,
					     void *obj, data_t *dst,
					     args_t *args)
{
	uint32_t *rpc_user_ave_time;
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->rpc_user_size)
		return SLURM_SUCCESS;

	data_set_list(dst);

	rpc_user_ave_time =
		xcalloc(stats->rpc_user_size, sizeof(*rpc_user_ave_time));

	for (int i = 0; i < stats->rpc_user_size; i++) {
		if ((stats->rpc_user_time[i] > 0) &&
		    (stats->rpc_user_cnt[i] > 0))
			rpc_user_ave_time[i] = stats->rpc_user_time[i] /
					       stats->rpc_user_cnt[i];
		else
			rpc_user_ave_time[i] = 0;
	}

	for (int i = 0; i < stats->rpc_user_size; i++) {
		data_t *u = data_set_dict(data_list_append(dst));
		data_t *un = data_key_set(u, "user");
		char *user = uid_to_string_or_null(stats->rpc_user_id[i]);

		data_set_int(data_key_set(u, "user_id"), stats->rpc_user_id[i]);
		data_set_int(data_key_set(u, "count"), stats->rpc_user_cnt[i]);
		data_set_int(data_key_set(u, "average_time"),
			     rpc_user_ave_time[i]);
		data_set_int(data_key_set(u, "total_time"),
			     stats->rpc_user_time[i]);

		if (!user)
			data_set_string_fmt(un, "%u", stats->rpc_user_id[i]);
		else
			data_set_string_own(un, user);
	}

	xfree(rpc_user_ave_time);
	return SLURM_SUCCESS;
}

typedef struct {
	int magic; /* MAGIC_FOREACH_CSV_LIST */
	int rc;
	char *dst;
	char *pos;
	const parser_t *const parser;
	args_t *args;
	data_t *parent_path;
} parse_foreach_CSV_LIST_t;

static data_for_each_cmd_t _parse_foreach_CSV_LIST_list(data_t *data, void *arg)
{
	parse_foreach_CSV_LIST_t *args = arg;

	if (data_convert_type(data, DATA_TYPE_STRING) != DATA_TYPE_STRING) {
		args->rc = on_error(PARSING, args->parser->type, args->args,
				    ESLURM_DATA_CONV_FAILED, NULL, __func__,
				    "unable to convert csv entry %s to string",
				    data_type_to_string(data_get_type(data)));
		return DATA_FOR_EACH_FAIL;
	}

	xstrfmtcatat(args->dst, &args->pos, "%s%s", (args->dst ? "," : ""),
		     data_get_string(data));

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _parse_foreach_CSV_LIST_dict(const char *key,
							data_t *data, void *arg)
{
	parse_foreach_CSV_LIST_t *args = arg;

	if (data_convert_type(data, DATA_TYPE_STRING) != DATA_TYPE_STRING) {
		args->rc = on_error(PARSING, args->parser->type, args->args,
				    ESLURM_DATA_CONV_FAILED, NULL, __func__,
				    "unable to convert csv entry %s to string",
				    data_type_to_string(data_get_type(data)));
		return DATA_FOR_EACH_FAIL;
	}

	xstrfmtcatat(args->dst, &args->pos, "%s%s=%s", (args->dst ? "," : ""),
		     key, data_get_string(data));

	return DATA_FOR_EACH_CONT;
}

static int PARSE_FUNC(CSV_LIST)(const parser_t *const parser, void *obj,
				data_t *src, args_t *args, data_t *parent_path)
{
	char **dst = obj;
	parse_foreach_CSV_LIST_t pargs = {
		.magic = MAGIC_FOREACH_CSV_LIST,
		.parser = parser,
		.args = args,
		.parent_path = parent_path,
	};

	xassert(args->magic == MAGIC_ARGS);
	xassert(!*dst);

	xfree(*dst);

	if (data_get_type(src) == DATA_TYPE_LIST) {
		(void) data_list_for_each(src, _parse_foreach_CSV_LIST_list,
					  &pargs);
	} else if (data_get_type(src) == DATA_TYPE_DICT) {
		(void) data_dict_for_each(src, _parse_foreach_CSV_LIST_dict,
					  &pargs);
	} else if (data_convert_type(src, DATA_TYPE_STRING) ==
		   DATA_TYPE_STRING) {
		*dst = xstrdup(data_get_string(src));
		return SLURM_SUCCESS;
	} else {
		return on_error(PARSING, parser->type, args, ESLURM_DATA_CONV_FAILED,
				NULL, __func__,
				"Expected dictionary or list or string for comma delimited list but got %s",
				data_type_to_string(data_get_type(src)));
	}

	if (!pargs.rc)
		*dst = pargs.dst;
	else
		xfree(pargs.dst);

	return pargs.rc;
}

static int DUMP_FUNC(CSV_LIST)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	char **src_ptr = obj;
	char *src = *src_ptr;
	char *save_ptr = NULL;
	char *token = NULL;
	char *str;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	if (!src || (src[0] == '\0'))
		return SLURM_SUCCESS;

	str = xstrdup(src);
	token = strtok_r(str, ",", &save_ptr);
	while (token) {
		data_set_string(data_list_append(dst), token);
		token = strtok_r(NULL, ",", &save_ptr);
	}

	xfree(str);
	return SLURM_SUCCESS;
}

PARSE_DISABLED(NODE_SELECT_ALLOC_MEMORY)

static int DUMP_FUNC(NODE_SELECT_ALLOC_MEMORY)(const parser_t *const parser,
					       void *obj, data_t *dst,
					       args_t *args)
{
	int rc;
	node_info_t *node = obj;
	uint64_t alloc_memory = 0;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((rc = slurm_get_select_nodeinfo(node->select_nodeinfo,
					    SELECT_NODEDATA_MEM_ALLOC,
					    NODE_STATE_ALLOCATED,
					    &alloc_memory))) {
		return on_error(
			DUMPING, parser->type, args, rc,
			"slurm_get_select_nodeinfo", __func__,
			"slurm_get_select_nodeinfo(%s, SELECT_NODEDATA_MEM_ALLOC) failed",
			node->name);
	}

	data_set_int(dst, alloc_memory);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(NODE_SELECT_ALLOC_CPUS)

static int DUMP_FUNC(NODE_SELECT_ALLOC_CPUS)(const parser_t *const parser,
					     void *obj, data_t *dst,
					     args_t *args)
{
	int rc;
	node_info_t *node = obj;
	uint16_t alloc_cpus = 0;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((rc = slurm_get_select_nodeinfo(node->select_nodeinfo,
					    SELECT_NODEDATA_SUBCNT,
					    NODE_STATE_ALLOCATED,
					    &alloc_cpus))) {
		return on_error(DUMPING, parser->type, args, rc,
				"slurm_get_select_nodeinfo", __func__,
				"slurm_get_select_nodeinfo(%s, SELECT_NODEDATA_SUBCNT) failed",
				node->name);
	}

	data_set_int(dst, alloc_cpus);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(NODE_SELECT_ALLOC_IDLE_CPUS)

static int DUMP_FUNC(NODE_SELECT_ALLOC_IDLE_CPUS)(const parser_t *const parser,
						  void *obj, data_t *dst,
						  args_t *args)
{
	int rc;
	node_info_t *node = obj;
	uint16_t alloc_cpus = 0;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((rc = slurm_get_select_nodeinfo(node->select_nodeinfo,
					    SELECT_NODEDATA_SUBCNT,
					    NODE_STATE_ALLOCATED,
					    &alloc_cpus))) {
		return on_error(DUMPING, parser->type, args, rc,
				"slurm_get_select_nodeinfo", __func__,
				"slurm_get_select_nodeinfo(%s, SELECT_NODEDATA_SUBCNT) failed",
				node->name);
	}

	data_set_int(dst, (node->cpus - alloc_cpus));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(NODE_SELECT_TRES_USED)

static int DUMP_FUNC(NODE_SELECT_TRES_USED)(const parser_t *const parser,
					    void *obj, data_t *dst,
					    args_t *args)
{
	int rc;
	node_info_t *node = obj;
	char *node_alloc_tres = NULL;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((rc = slurm_get_select_nodeinfo(node->select_nodeinfo,
					    SELECT_NODEDATA_TRES_ALLOC_FMT_STR,
					    NODE_STATE_ALLOCATED,
					    &node_alloc_tres))) {
		return on_error(DUMPING, parser->type, args, rc,
				"slurm_get_select_nodeinfo", __func__,
				"slurm_get_select_nodeinfo(%s, SELECT_NODEDATA_TRES_ALLOC_FMT_STR) failed",
				node->name);
	}

	data_set_string_own(dst, node_alloc_tres);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(NODE_SELECT_TRES_WEIGHTED)

static int DUMP_FUNC(NODE_SELECT_TRES_WEIGHTED)(const parser_t *const parser,
						void *obj, data_t *dst,
						args_t *args)
{
	int rc;
	node_info_t *node = obj;
	double node_tres_weighted = 0;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((rc = slurm_get_select_nodeinfo(node->select_nodeinfo,
					    SELECT_NODEDATA_TRES_ALLOC_WEIGHTED,
					    NODE_STATE_ALLOCATED,
					    &node_tres_weighted))) {
		return on_error(DUMPING, parser->type, args, rc,
				"slurm_get_select_nodeinfo", __func__,
				"slurm_get_select_nodeinfo(%s, SELECT_NODEDATA_TRES_ALLOC_WEIGHTED) failed",
				node->name);
	}

	data_set_float(dst, node_tres_weighted);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(NODES)

static int DUMP_FUNC(NODES)(const parser_t *const parser, void *obj,
			    data_t *dst, args_t *args)
{
	node_info_msg_t *nodes = obj;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	if (!nodes || !nodes->record_count) {
		on_warn(DUMPING, parser->type, args, NULL, __func__,
			"No nodes to dump");
		return SLURM_SUCCESS;
	}

	for (int i = 0; !rc && (i < nodes->record_count); i++)
		rc = DUMP(NODE, nodes->node_array[i], data_list_append(dst),
			  args);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(LICENSES)

static int DUMP_FUNC(LICENSES)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	int rc = SLURM_SUCCESS;
	license_info_msg_t *msg = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	if (!msg->num_lic) {
		on_warn(DUMPING, parser->type, args, NULL, __func__,
			"Zero licenses to dump");
		return SLURM_SUCCESS;
	}

	for (size_t i = 0; !rc && (i < msg->num_lic); i++)
		rc = DUMP(LICENSE, msg->lic_array[i], data_list_append(dst),
			  args);

	return rc;
}

static int PARSE_FUNC(CORE_SPEC)(const parser_t *const parser, void *obj,
				 data_t *src, args_t *args, data_t *parent_path)
{
	uint16_t *spec = obj;

	if (data_convert_type(src, DATA_TYPE_INT_64) != DATA_TYPE_INT_64)
		return on_error(PARSING, parser->type, args, ESLURM_DATA_CONV_FAILED,
				NULL, __func__,
				"Expected integer for core specification but got %s",
				data_type_to_string(data_get_type(src)));

	if (data_get_int(src) >= CORE_SPEC_THREAD)
		return on_error(PARSING, parser->type, args,
				ESLURM_INVALID_CORE_CNT, NULL, __func__,
				"Invalid core specification %"PRId64" >= %d",
				data_get_int(src), CORE_SPEC_THREAD);

	if (data_get_int(src) <= 0)
		return on_error(PARSING, parser->type, args,
				ESLURM_INVALID_CORE_CNT, NULL, __func__,
				"Invalid core specification %"PRId64" <= 0",
				data_get_int(src));

	*spec = data_get_int(src);
	return SLURM_SUCCESS;
}

static int DUMP_FUNC(CORE_SPEC)(const parser_t *const parser, void *obj,
				data_t *dst, args_t *args)
{
	uint16_t *mem = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!(*mem & CORE_SPEC_THREAD))
		data_set_int(dst, *mem);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(THREAD_SPEC)(const parser_t *const parser, void *obj,
				   data_t *src, args_t *args,
				   data_t *parent_path)
{
	uint16_t *spec = obj;

	if (data_convert_type(src, DATA_TYPE_INT_64) != DATA_TYPE_INT_64)
		return on_error(PARSING, parser->type, args, ESLURM_DATA_CONV_FAILED,
				NULL, __func__,
				"Expected integer for thread specification but got %s",
				data_type_to_string(data_get_type(src)));

	if (data_get_int(src) >= CORE_SPEC_THREAD)
		return on_error(PARSING, parser->type, args,
				ESLURM_BAD_THREAD_PER_CORE, NULL, __func__,
				"Invalid thread specification %"PRId64" >= %d",
				data_get_int(src), CORE_SPEC_THREAD);

	if (data_get_int(src) <= 0)
		return on_error(PARSING, parser->type, args,
				ESLURM_BAD_THREAD_PER_CORE, NULL, __func__,
				"Invalid thread specification %"PRId64"<= 0",
				data_get_int(src));

	*spec = data_get_int(src);
	*spec |= CORE_SPEC_THREAD;
	return SLURM_SUCCESS;
}

static int DUMP_FUNC(THREAD_SPEC)(const parser_t *const parser, void *obj,
				  data_t *dst, args_t *args)
{
	uint16_t *mem = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((*mem & CORE_SPEC_THREAD))
		data_set_int(dst, (*mem & ~CORE_SPEC_THREAD));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_INFO_GRES_DETAIL)

static int DUMP_FUNC(JOB_INFO_GRES_DETAIL)(const parser_t *const parser,
					   void *obj, data_t *dst, args_t *args)
{
	slurm_job_info_t *job = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(job);

	data_set_list(dst);

	for (int i = 0; i < job->gres_detail_cnt; i++)
		data_set_string(data_list_append(dst), job->gres_detail_str[i]);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(NICE)

static int DUMP_FUNC(NICE)(const parser_t *const parser, void *obj, data_t *dst,
			   args_t *args)
{
	uint32_t *nice = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((*nice != NO_VAL) && (*nice != NICE_OFFSET))
		data_set_int(dst, (*nice - NICE_OFFSET));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_MEM_PER_CPU)

static int DUMP_FUNC(JOB_MEM_PER_CPU)(const parser_t *const parser, void *obj,
				      data_t *dst, args_t *args)
{
	uint64_t *mem = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (*mem & MEM_PER_CPU)
		data_set_int(dst, (*mem & ~MEM_PER_CPU));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_MEM_PER_NODE)

static int DUMP_FUNC(JOB_MEM_PER_NODE)(const parser_t *const parser, void *obj,
				       data_t *dst, args_t *args)
{
	uint64_t *mem = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!(*mem & MEM_PER_CPU))
		data_set_int(dst, *mem);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(ACCT_GATHER_PROFILE)

static int DUMP_FUNC(ACCT_GATHER_PROFILE)(const parser_t *const parser,
					  void *obj, data_t *dst, args_t *args)
{
	uint32_t *profile = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (*profile == ACCT_GATHER_PROFILE_NOT_SET)
		return SLURM_SUCCESS;

	data_set_list(dst);

	if (*profile == ACCT_GATHER_PROFILE_NONE) {
		data_set_string(data_list_append(dst), "None");
		return SLURM_SUCCESS;
	}

	if (*profile & ACCT_GATHER_PROFILE_ENERGY)
		data_set_string(data_list_append(dst), "Energy");
	if (*profile & ACCT_GATHER_PROFILE_LUSTRE)
		data_set_string(data_list_append(dst), "Lustre");
	if (*profile & ACCT_GATHER_PROFILE_NETWORK)
		data_set_string(data_list_append(dst), "Network");
	if (*profile & ACCT_GATHER_PROFILE_TASK)
		data_set_string(data_list_append(dst), "Task");

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_SHARED)

static int DUMP_FUNC(JOB_SHARED)(const parser_t *const parser, void *obj,
				 data_t *dst, args_t *args)
{
	uint16_t *shared = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (*shared == NO_VAL16)
		return SLURM_SUCCESS;

	switch (*shared) {
	case JOB_SHARED_NONE :
		data_set_string(dst, "none");
		break;
	case JOB_SHARED_OK :
		data_set_string(dst, "shared");
		break;
	case JOB_SHARED_USER :
		data_set_string(dst, "user");
		break;
	case JOB_SHARED_MCS :
		data_set_string(dst, "mcs");
		break;
	default :
		return ESLURM_DATA_FLAGS_INVALID_TYPE;
	}

	return SLURM_SUCCESS;
}

PARSE_DISABLED(ALLOCATED_CORES)

static int DUMP_FUNC(ALLOCATED_CORES)(const parser_t *const parser, void *obj,
				      data_t *dst, args_t *args)
{
	uint32_t *cores = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (slurm_conf.select_type_param & (CR_CORE | CR_SOCKET))
		data_set_int(dst, *cores);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(ALLOCATED_CPUS)

static int DUMP_FUNC(ALLOCATED_CPUS)(const parser_t *const parser, void *obj,
				     data_t *dst, args_t *args)
{
	uint32_t *cpus = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (slurm_conf.select_type_param & (CR_CPU))
		data_set_int(dst, *cpus);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_RES_PTR)

static int DUMP_FUNC(JOB_RES_PTR)(const parser_t *const parser, void *obj,
				  data_t *dst, args_t *args)
{
	job_resources_t **res = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(res);

	if (!*res)
		return SLURM_SUCCESS;

	return DUMP(JOB_RES, **res, dst, args);
}

static void _dump_node_res(data_t *dnodes, job_resources_t *j,
			   const size_t node_inx, const char *nodename,
			   const size_t sock_inx, size_t *bit_inx,
			   const size_t array_size)
{
	size_t bit_reps;
	data_t *dnode = data_set_dict(data_list_append(dnodes));
	data_t *dsockets = data_set_dict(data_key_set(dnode, "sockets"));
	data_t **sockets;

	sockets = xcalloc(j->sockets_per_node[sock_inx], sizeof(*sockets));

	/* per node */

	data_set_string(data_key_set(dnode, "nodename"), nodename);

	data_set_int(data_key_set(dnode, "cpus_used"), j->cpus_used[node_inx]);
	data_set_int(data_key_set(dnode, "memory_used"),
		     j->memory_used[node_inx]);
	data_set_int(data_key_set(dnode, "memory_allocated"),
		     j->memory_allocated[node_inx]);

	/* set the used cores as found */

	bit_reps =
		j->sockets_per_node[sock_inx] * j->cores_per_socket[sock_inx];
	for (size_t i = 0; i < bit_reps; i++) {
		size_t socket_inx = i / j->cores_per_socket[sock_inx];
		size_t core_inx = i % j->cores_per_socket[sock_inx];

		xassert(*bit_inx < array_size);

		if (*bit_inx >= array_size) {
			error("%s: unexpected invalid bit index:%zu/%zu",
			      __func__, *bit_inx, array_size);
			break;
		}

		if (bit_test(j->core_bitmap, *bit_inx)) {
			data_t *dcores;

			if (!sockets[socket_inx]) {
				sockets[socket_inx] = data_set_dict(
					data_key_set_int(dsockets, socket_inx));
				dcores = data_set_dict(data_key_set(
					sockets[socket_inx], "cores"));
			} else {
				dcores = data_key_get(sockets[socket_inx],
						      "cores");
			}

			if (bit_test(j->core_bitmap_used, *bit_inx)) {
				data_set_string(data_key_set_int(dcores,
								 core_inx),
						"allocated_and_in_use");
			} else {
				data_set_string(data_key_set_int(dcores,
								 core_inx),
						"allocated");
			}
		}

		(*bit_inx)++;
	}

	xfree(sockets);
}

PARSE_DISABLED(JOB_RES_NODES)

static int DUMP_FUNC(JOB_RES_NODES)(const parser_t *const parser, void *obj,
				    data_t *dst, args_t *args)
{
	job_resources_t *j = obj;
	hostlist_t hl = NULL;
	size_t bit_inx = 0;
	size_t array_size;
	size_t sock_inx = 0, sock_reps = 0;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(j);
	data_set_list(dst);

	/* log_job_resources() used as an example */

	if (!j->cores_per_socket || !j->nhosts) {
		/* not enough info present */
		return SLURM_SUCCESS;
	}

	hl = hostlist_create(j->nodes);
	array_size = bit_size(j->core_bitmap);

	for (size_t node_inx = 0; node_inx < j->nhosts; node_inx++) {
		char *nodename = hostlist_nth(hl, node_inx);

		if (sock_reps >= j->sock_core_rep_count[sock_inx]) {
			sock_inx++;
			sock_reps = 0;
		}
		sock_reps++;

		_dump_node_res(dst, j, node_inx, nodename, sock_inx, &bit_inx,
			       array_size);

		free(nodename);
	}

	FREE_NULL_HOSTLIST(hl);
	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_INFO_MSG)

static int DUMP_FUNC(JOB_INFO_MSG)(const parser_t *const parser, void *obj,
				   data_t *dst, args_t *args)
{
	int rc = SLURM_SUCCESS;
	job_info_msg_t *msg = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	if (!msg || !msg->record_count) {
		on_warn(DUMPING, parser->type, args, NULL, __func__,
			"Zero jobs to dump");
		return SLURM_SUCCESS;
	}

	for (size_t i = 0; !rc && (i < msg->record_count); ++i)
		rc = DUMP(JOB_INFO, msg->job_array[i], data_list_append(dst),
			  args);

	return rc;
}

PARSE_DISABLED(CONTROLLER_PING_ARRAY)

static int DUMP_FUNC(CONTROLLER_PING_ARRAY)(const parser_t *const parser,
					    void *obj, data_t *dst,
					    args_t *args)
{
	int rc = SLURM_SUCCESS;
	controller_ping_t **ping_ptr = obj;
	controller_ping_t *ping = *ping_ptr;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	for (; !rc && ping && ping->hostname; ping++) {
		rc = DUMP(CONTROLLER_PING, *ping,
			  data_set_dict(data_list_append(dst)), args);
	}

	return rc;
}

PARSE_DISABLED(CONTROLLER_PING_MODE)

static int DUMP_FUNC(CONTROLLER_PING_MODE)(const parser_t *const parser,
					   void *obj, data_t *dst, args_t *args)
{
	int *mode_ptr = obj;
	int mode = *mode_ptr;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (mode == 0)
		data_set_string(dst, "primary");
	else if ((mode == 1) && (slurm_conf.control_cnt == 2))
		data_set_string(dst, "backup");
	else
		data_set_string_fmt(dst, "backup%u", mode);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(CONTROLLER_PING_RESULT)

static int DUMP_FUNC(CONTROLLER_PING_RESULT)(const parser_t *const parser,
					     void *obj, data_t *dst,
					     args_t *args)
{
	bool *ping_ptr = obj;
	int ping = *ping_ptr;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (ping)
		data_set_string(dst, "UP");
	else
		data_set_string(dst, "DOWN");

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STEP_INFO_MSG)

static int DUMP_FUNC(STEP_INFO_MSG)(const parser_t *const parser, void *obj,
				    data_t *dst, args_t *args)
{
	int rc = SLURM_SUCCESS;
	job_step_info_response_msg_t **msg = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	if (!*msg || !(*msg)->job_step_count) {
		on_warn(DUMPING, parser->type, args, NULL, __func__,
			"Zero steps to dump");
		return SLURM_SUCCESS;
	}

	for (size_t i = 0; !rc && (i < (*msg)->job_step_count); ++i)
		rc = DUMP(STEP_INFO, (*msg)->job_steps[i],
			  data_list_append(dst), args);

	return rc;
}

PARSE_DISABLED(HOSTLIST)

static int DUMP_FUNC(HOSTLIST)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	int rc = SLURM_SUCCESS;
	hostlist_t *host_list_ptr = obj;
	hostlist_t host_list = *host_list_ptr;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	if (hostlist_count(host_list)) {
		char *host;
		hostlist_iterator_t itr = hostlist_iterator_create(host_list);

		while ((host = hostlist_next(itr))) {
			data_set_string(data_list_append(dst), host);
			free(host);
		}

		hostlist_iterator_destroy(itr);
	}

	return rc;
}

PARSE_DISABLED(CPU_FREQ_FLAGS)

static int DUMP_FUNC(CPU_FREQ_FLAGS)(const parser_t *const parser, void *obj,
				     data_t *dst, args_t *args)
{
	uint32_t *freq_ptr = obj;
	char *buf = xmalloc(BUF_SIZE);

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	cpu_freq_to_string(buf, (BUF_SIZE - 1), *freq_ptr);
	data_set_string_own(dst, buf);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(PARTITION_INFO_PTR)

static int DUMP_FUNC(PARTITION_INFO_PTR)(const parser_t *const parser,
					 void *obj, data_t *dst, args_t *args)
{
	partition_info_t **ptr = obj;
	partition_info_t *part = *ptr;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!part)
		return SLURM_SUCCESS;

	return DUMP(PARTITION_INFO, *part, dst, args);
}

PARSE_DISABLED(NODE_ARRAY)

static int DUMP_FUNC(NODE_ARRAY)(const parser_t *const parser, void *obj,
				 data_t *dst, args_t *args)
{
	int rc = SLURM_SUCCESS;
	node_info_t ***ptr = obj;
	node_info_t **nodes = *ptr;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	for (int i = 0; !rc && nodes[i]; i++)
		rc = DUMP(NODE, *nodes[i], data_list_append(dst), args);

	return rc;
}

PARSE_DISABLED(PARTITION_INFO_ARRAY)

static int DUMP_FUNC(PARTITION_INFO_ARRAY)(const parser_t *const parser,
					   void *obj, data_t *dst, args_t *args)
{
	int rc = SLURM_SUCCESS;
	partition_info_t ***ptr = obj;
	partition_info_t **parts = *ptr;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	if (!parts || !parts[0]) {
		on_warn(DUMPING, parser->type, args, NULL, __func__,
			"No partitions to dump");
		return SLURM_SUCCESS;
	}

	for (int i = 0; !rc && parts[i]; i++)
		rc = DUMP(PARTITION_INFO, *parts[i], data_list_append(dst),
			  args);

	return rc;
}

PARSE_DISABLED(PARTITION_INFO_MSG)

static int DUMP_FUNC(PARTITION_INFO_MSG)(const parser_t *const parser,
					 void *obj, data_t *dst, args_t *args)
{
	int rc = SLURM_SUCCESS;
	partition_info_msg_t *msg = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	if (!msg || !msg->record_count) {
		on_warn(DUMPING, parser->type, args, NULL, __func__,
			"No partitions to dump");
		return SLURM_SUCCESS;
	}

	for (uint32_t i = 0; !rc && (i < msg->record_count); ++i)
		rc = DUMP(PARTITION_INFO, msg->partition_array[i],
			  data_list_append(dst), args);

	return rc;
}

PARSE_DISABLED(STEP_INFO_ARRAY)

static int DUMP_FUNC(STEP_INFO_ARRAY)(const parser_t *const parser, void *obj,
				      data_t *dst, args_t *args)
{
	int rc = SLURM_SUCCESS;
	job_step_info_t ***ptr = obj;
	job_step_info_t **steps = *ptr;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	if (!steps || !*steps) {
		on_warn(DUMPING, parser->type, args, NULL, __func__,
			"Zero steps to dump");
		return SLURM_SUCCESS;
	}

	for (int i = 0; !rc && steps[i]; i++)
		rc = DUMP(STEP_INFO, *steps[i], data_list_append(dst), args);

	return rc;
}

PARSE_DISABLED(ACCT_GATHER_ENERGY_PTR)

static int DUMP_FUNC(ACCT_GATHER_ENERGY_PTR)(const parser_t *const parser,
					     void *obj, data_t *dst,
					     args_t *args)
{
	acct_gather_energy_t **ptr = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (*ptr)
		return DUMP(ACCT_GATHER_ENERGY, **ptr, dst, args);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(EXT_SENSORS_DATA_PTR)

static int DUMP_FUNC(EXT_SENSORS_DATA_PTR)(const parser_t *const parser,
					   void *obj, data_t *dst, args_t *args)
{
	ext_sensors_data_t **ptr = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (*ptr)
		return DUMP(EXT_SENSORS_DATA, **ptr, dst, args);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(POWER_MGMT_DATA_PTR)

static int DUMP_FUNC(POWER_MGMT_DATA_PTR)(const parser_t *const parser,
					  void *obj, data_t *dst, args_t *args)
{
	power_mgmt_data_t **ptr = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (*ptr)
		return DUMP(POWER_MGMT_DATA, **ptr, dst, args);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(NODE_STATES_NO_VAL)

static int DUMP_FUNC(NODE_STATES_NO_VAL)(const parser_t *const parser,
					 void *obj, data_t *dst, args_t *args)
{
	uint32_t *ptr = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (*ptr != NO_VAL)
		return DUMP(NODE_STATES, *ptr, dst, args);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(RESERVATION_INFO_MSG)

static int DUMP_FUNC(RESERVATION_INFO_MSG)(const parser_t *const parser,
					   void *obj, data_t *dst, args_t *args)
{
	int rc = SLURM_SUCCESS;
	reserve_info_msg_t *res = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	for (int i = 0; !rc && (i < res->record_count); i++)
		rc = DUMP(RESERVATION_INFO, res->reservation_array[i],
			  data_list_append(dst), args);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(RESERVATION_INFO_CORE_SPEC)

static int DUMP_FUNC(RESERVATION_INFO_CORE_SPEC)(const parser_t *const parser,
						 void *obj, data_t *dst,
						 args_t *args)
{
	int rc = SLURM_SUCCESS;
	reserve_info_t *res = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	for (int i = 0; !rc && (i < res->core_spec_cnt); i++)
		rc = DUMP(RESERVATION_CORE_SPEC, res->core_spec[i],
			  data_list_append(dst), args);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(RESERVATION_INFO_ARRAY)

static int DUMP_FUNC(RESERVATION_INFO_ARRAY)(const parser_t *const parser,
					     void *obj, data_t *dst,
					     args_t *args)
{
	int rc = SLURM_SUCCESS;
	reserve_info_t ***ptr = obj;
	reserve_info_t **res = *ptr;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	for (int i = 0; !rc && res[i]; i++)
		rc = DUMP(RESERVATION_INFO, *res[i], data_list_append(dst),
			  args);

	return rc;
}

PARSE_DISABLED(JOB_ARRAY_RESPONSE_MSG)

static int DUMP_FUNC(JOB_ARRAY_RESPONSE_MSG)(const parser_t *const parser,
					     void *obj, data_t *dst,
					     args_t *args)
{
	job_array_resp_msg_t *msg = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	for (int i = 0; i < msg->job_array_count; i++) {
		data_t *j = data_set_dict(data_list_append(dst));
		data_set_string(data_key_set(j, "job_id"),
				msg->job_array_id[i]);
		data_set_int(data_key_set(j, "error_code"), msg->error_code[i]);
		data_set_string(data_key_set(j, "error"),
				slurm_strerror(msg->error_code[i]));
		data_set_string(data_key_set(j, "why"), msg->err_msg[i]);
	}

	return SLURM_SUCCESS;
}

PARSE_DISABLED(ERROR)

static int DUMP_FUNC(ERROR)(const parser_t *const parser, void *obj,
			    data_t *dst, args_t *args)
{
	uint32_t *rc = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_string(dst, slurm_strerror(*rc));

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(JOB_DESC_MSG_ARGV)(const parser_t *const parser,
					 void *obj, data_t *src, args_t *args,
					 data_t *parent_path)
{
	int rc;
	job_desc_msg_t *job = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) == DATA_TYPE_NULL) {
		xassert(!job->argv);
		xassert(!job->argc);
		return SLURM_SUCCESS;
	}

	rc = PARSE(STRING_ARRAY, job->argv, src, parent_path, args);
	job->argc = envcount(job->environment);

	return rc;
}

static int DUMP_FUNC(JOB_DESC_MSG_ARGV)(const parser_t *const parser, void *obj,
					data_t *dst, args_t *args)
{
	job_desc_msg_t *job = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!job || !job->argv)
		return SLURM_SUCCESS;

	return DUMP(STRING_ARRAY, job->argv, dst, args);
}

static int PARSE_FUNC(JOB_DESC_MSG_CPU_FREQ)(const parser_t *const parser,
					     void *obj, data_t *src,
					     args_t *args, data_t *parent_path)
{
	int rc;
	job_desc_msg_t *job = obj;
	char *str = NULL;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) == DATA_TYPE_NULL) {
		job->cpu_freq_min = NO_VAL;
		job->cpu_freq_max = NO_VAL;
		job->cpu_freq_gov = NO_VAL;
		return SLURM_SUCCESS;
	}

	if ((rc = data_get_string_converted(src, &str)))
		return on_error(PARSING, parser->type, args, rc,
				"data_get_string_converted()", __func__,
				"string expected but got %s",
				data_type_to_string(data_get_type(src)));

	if ((rc = cpu_freq_verify_cmdline(str, &job->cpu_freq_min,
					  &job->cpu_freq_max,
					  &job->cpu_freq_gov))) {
		xfree(str);
		return on_error(PARSING, parser->type, args, rc,
				"cpu_freq_verify_cmdline()", __func__,
				"Invalid cpu_freuency");
	}

	xfree(str);
	return rc;
}

static int DUMP_FUNC(JOB_DESC_MSG_CPU_FREQ)(const parser_t *const parser,
					    void *obj, data_t *dst,
					    args_t *args)
{
	job_desc_msg_t *job = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (job->cpu_freq_min || job->cpu_freq_max || job->cpu_freq_gov) {
		char *tmp = cpu_freq_to_cmdline(job->cpu_freq_min,
						job->cpu_freq_max,
						job->cpu_freq_gov);

		if (tmp)
			data_set_string_own(dst, tmp);
	}

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(JOB_DESC_MSG_ENV)(const parser_t *const parser, void *obj,
					data_t *src, args_t *args,
					data_t *parent_path)
{
	int rc;
	job_desc_msg_t *job = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) == DATA_TYPE_NULL) {
		xassert(!job->environment);
		xassert(!job->env_size);
		return SLURM_SUCCESS;
	}

	rc = PARSE(STRING_ARRAY, job->environment, src, parent_path, args);
	job->env_size = envcount(job->environment);

	return rc;
}

static int DUMP_FUNC(JOB_DESC_MSG_ENV)(const parser_t *const parser, void *obj,
				       data_t *dst, args_t *args)
{
	job_desc_msg_t *job = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!job || !job->environment)
		return SLURM_SUCCESS;

	return DUMP(STRING_ARRAY, job->environment, dst, args);
}

static int PARSE_FUNC(JOB_DESC_MSG_SPANK_ENV)(const parser_t *const parser,
					      void *obj, data_t *src,
					      args_t *args, data_t *parent_path)
{
	int rc;
	job_desc_msg_t *job = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) == DATA_TYPE_NULL) {
		xassert(!job->spank_job_env);
		xassert(!job->spank_job_env_size);
		return SLURM_SUCCESS;
	}

	rc = PARSE(STRING_ARRAY, job->spank_job_env, src, parent_path, args);
	job->spank_job_env_size = envcount(job->spank_job_env);

	return rc;
}

static int DUMP_FUNC(JOB_DESC_MSG_SPANK_ENV)(const parser_t *const parser,
					     void *obj, data_t *dst,
					     args_t *args)
{
	job_desc_msg_t *job = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!job || !job->spank_job_env)
		return SLURM_SUCCESS;

	return DUMP(STRING_ARRAY, job->spank_job_env, dst, args);
}

static int PARSE_FUNC(JOB_DESC_MSG_PTR)(const parser_t *const parser, void *obj,
					data_t *src, args_t *args,
					data_t *parent_path)
{
	int rc;
	job_desc_msg_t **ptr = obj;
	job_desc_msg_t *job = NULL;

	xassert(args->magic == MAGIC_ARGS);
	xassert(!*ptr);

	rc = PARSE(JOB_DESC_MSG, *job, src, parent_path, args);

	if (job)
		*ptr = job;

	return rc;
}

static int DUMP_FUNC(JOB_DESC_MSG_PTR)(const parser_t *const parser, void *obj,
				       data_t *dst, args_t *args)
{
	job_desc_msg_t **ptr = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!*ptr)
		return SLURM_SUCCESS;

	return DUMP(JOB_DESC_MSG, **ptr, dst, args);
}

static data_for_each_cmd_t _foreach_string_array_list(const data_t *data,
						      void *arg)
{
	foreach_string_array_t *args = arg;
	char *str = NULL;
	int rc;

	xassert(args->magic == MAGIC_FOREACH_STRING_ARRAY);

	if ((rc = data_get_string_converted(data, &str))) {
		on_error(PARSING, args->parser->type, args->args, rc,
			 "data_get_string_converted()", __func__,
			 "expected string but got %s",
			 data_type_to_string(data_get_type(data)));
		return DATA_FOR_EACH_FAIL;
	}

	args->array[args->i] = str;
	args->i++;

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _foreach_string_array_dict(const char *key,
						      const data_t *data,
						      void *arg)
{
	foreach_string_array_t *args = arg;
	char *str = NULL, *keyvalue = NULL;
	int rc;

	xassert(args->magic == MAGIC_FOREACH_STRING_ARRAY);

	if ((rc = data_get_string_converted(data, &str))) {
		on_error(PARSING, args->parser->type, args->args, rc,
			 "data_get_string_converted()", __func__,
			 "expected string but got %s",
			 data_type_to_string(data_get_type(data)));
		return DATA_FOR_EACH_FAIL;
	}

	xstrfmtcat(keyvalue, "%s=%s", key, str);

	args->array[args->i] = keyvalue;
	args->i++;

	xfree(str);

	return DATA_FOR_EACH_CONT;
}

static int PARSE_FUNC(STRING_ARRAY)(const parser_t *const parser, void *obj,
				    data_t *src, args_t *args,
				    data_t *parent_path)
{
	char ***array_ptr = obj;
	foreach_string_array_t fargs = {
		.magic = MAGIC_FOREACH_STRING_ARRAY,
		.parser = parser,
		.args = args,
	};

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) == DATA_TYPE_LIST) {
		fargs.array = xcalloc(data_get_list_length(src) + 1,
				      sizeof(*fargs.array));

		if (data_list_for_each_const(src, _foreach_string_array_list,
					     &fargs) < 0)
			goto cleanup;
	} else if (data_get_type(src) == DATA_TYPE_DICT) {
		fargs.array = xcalloc(data_get_dict_length(src) + 1,
				      sizeof(*fargs.array));

		if (data_dict_for_each_const(src, _foreach_string_array_dict,
					     &fargs) < 0)
			goto cleanup;
	} else {
		on_error(PARSING, parser->type, args, ESLURM_DATA_EXPECTED_LIST,
			 NULL, __func__,
			 "expected a list of strings but got %s",
			 data_type_to_string(data_get_type(src)));
		goto cleanup;
	}

	xassert(!*array_ptr);
	*array_ptr = fargs.array;

	return SLURM_SUCCESS;
cleanup:
	for (int i = 0; fargs.array[i]; i++)
		xfree(fargs.array[i]);
	xfree(fargs.array);

	return ESLURM_DATA_CONV_FAILED;
}

static int DUMP_FUNC(STRING_ARRAY)(const parser_t *const parser, void *obj,
				   data_t *dst, args_t *args)
{
	char ***array_ptr = obj;
	char **array;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!(array = *array_ptr))
		return SLURM_SUCCESS;

	data_set_list(dst);

	for (int i = 0; array[i]; i++)
		data_set_string(data_list_append(dst), array[i]);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(SIGNAL)(const parser_t *const parser, void *obj,
			      data_t *src, args_t *args, data_t *parent_path)
{
	int rc;
	uint16_t *sig = obj;
	char *str = NULL;

	if (data_convert_type(src, DATA_TYPE_INT_64) == DATA_TYPE_INT_64) {
		*sig = data_get_int(src);
		return SLURM_SUCCESS;
	}

	if ((rc = data_get_string_converted(src, &str))) {
		return on_error(PARSING, parser->type, args, rc,
				"data_get_string_converted()", __func__,
				"expected string but got %s",
				data_type_to_string(data_get_type(src)));
	}

	if (!(*sig = sig_name2num(str))) {
		xfree(str);
		return on_error(PARSING, parser->type, args, rc,
				"sig_name2num()", __func__, "Unknown signal %s",
				str);
	}

	if ((*sig < 1) || (*sig >= SIGRTMAX)) {
		on_warn(PARSING, parser->type, args, NULL, __func__,
			"Non-standard signal number: %u", *sig);
	}

	xfree(str);
	return rc;
}

static int DUMP_FUNC(SIGNAL)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	uint16_t *sig = obj;

	data_set_string_own(dst, sig_num2name(*sig));

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(CRON_ENTRY_PTR)(const parser_t *const parser, void *obj,
				      data_t *src, args_t *args,
				      data_t *parent_path)
{
	cron_entry_t **ptr = obj;

	if (!*ptr)
		*ptr = new_cron_entry();

	return PARSE(CRON_ENTRY, **ptr, src, parent_path, args);
}

static int DUMP_FUNC(CRON_ENTRY_PTR)(const parser_t *const parser, void *obj,
				     data_t *dst, args_t *args)
{
	cron_entry_t **ptr = obj;

	if (!*ptr)
		return SLURM_SUCCESS;

	return DUMP(CRON_ENTRY, **ptr, dst, args);
}

static int PARSE_FUNC(BITSTR)(const parser_t *const parser, void *obj,
			      data_t *src, args_t *args, data_t *parent_path)
{
	int rc;
	bitstr_t *b = obj;

	xassert(*b);

	if (data_convert_type(src, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		return on_error(PARSING, parser->type, args,
				ESLURM_DATA_CONV_FAILED, NULL, __func__,
				"Expecting string but got %s",
				data_type_to_string(data_get_type(src)));

	rc = bit_unfmt(b, data_get_string(src));

	return rc;
}

static int DUMP_FUNC(BITSTR)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	bitstr_t *b = obj;

	if (!b)
		return SLURM_SUCCESS;

	data_set_string_own(dst, bit_fmt_full(b));

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(JOB_DESC_MSG_NODES)(const parser_t *const parser, void *obj,
			      data_t *src, args_t *args, data_t *parent_path)
{
	job_desc_msg_t *job = obj;

	if (data_get_type(src) == DATA_TYPE_LIST) {
		data_t *min, *max;

		if (!data_get_list_length(src) || (data_get_list_length(src) > 2)) {
			return on_error(PARSING, parser->type, args,
				      ESLURM_DATA_CONV_FAILED, NULL, __func__,
				      "Node count in format of a list must have a cardinality of 2 or 1");
		}

		min = data_list_dequeue(src);
		max = data_list_dequeue(src);

		if (!max)
			SWAP(min, max);

		if (min && (data_convert_type(min, DATA_TYPE_INT_64) != DATA_TYPE_INT_64))
			return on_error(PARSING, parser->type, args,
				      ESLURM_DATA_CONV_FAILED, NULL, __func__,
				      "Minimum nodes must be an integer instead of %s",
				      data_type_to_string(data_get_type(min)));
		if (max && (data_convert_type(max, DATA_TYPE_INT_64) != DATA_TYPE_INT_64))
			return on_error(PARSING, parser->type, args,
				      ESLURM_DATA_CONV_FAILED, NULL, __func__,
				      "Maximum nodes must be an integer instead of %s",
				      data_type_to_string(data_get_type(max)));

		job->max_nodes = data_get_int(max);
		if (min)
			job->min_nodes = data_get_int(min);
	} else {
		int min, max;

		if (data_convert_type(src, DATA_TYPE_STRING) != DATA_TYPE_STRING)
			return on_error(PARSING, parser->type, args,
					ESLURM_DATA_CONV_FAILED, NULL, __func__,
					"Expected string instead of %s for node counts",
					data_type_to_string(data_get_type(src)));

		if (!verify_node_count(data_get_string(src), &min, &max))
			return on_error(PARSING, parser->type, args,
					ESLURM_DATA_CONV_FAILED,
					"verify_node_count()",
					__func__, "Unknown format: %s",
					data_get_string(src));

		job->min_nodes = min;
		job->max_nodes = max;
	}

	if (job->min_nodes > job->max_nodes)
		SWAP(job->min_nodes, job->max_nodes);

	return SLURM_SUCCESS;
}

static int DUMP_FUNC(JOB_DESC_MSG_NODES)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	job_desc_msg_t *job = obj;

	if (job->min_nodes != job->max_nodes)
		data_set_string_own(dst,
				    xstrdup_printf("%d-%d", job->min_nodes,
						   job->max_nodes));
	else
		data_set_string_own(dst, xstrdup_printf("%d", job->min_nodes));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_INFO_STDIN)

static int DUMP_FUNC(JOB_INFO_STDIN)(const parser_t *const parser, void *obj,
			    data_t *dst, args_t *args)
{
	slurm_job_info_t *job = obj;
	char *str = xmalloc(PATH_MAX + 1);

	slurm_get_job_stdin(str, PATH_MAX, job);
	data_set_string_own(dst, str);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_INFO_STDOUT)

static int DUMP_FUNC(JOB_INFO_STDOUT)(const parser_t *const parser, void *obj,
			    data_t *dst, args_t *args)
{
	slurm_job_info_t *job = obj;
	char *str = xmalloc(PATH_MAX + 1);

	slurm_get_job_stdout(str, PATH_MAX, job);
	data_set_string_own(dst, str);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_INFO_STDERR)

static int DUMP_FUNC(JOB_INFO_STDERR)(const parser_t *const parser, void *obj,
			    data_t *dst, args_t *args)
{
	slurm_job_info_t *job = obj;
	char *str = xmalloc(PATH_MAX + 1);

	slurm_get_job_stderr(str, PATH_MAX, job);
	data_set_string_own(dst, str);

	return SLURM_SUCCESS;
}

/*
 * The following struct arrays are not following the normal Slurm style but are
 * instead being treated as piles of data instead of code.
 */
// clang-format off
#define add_parser(stype, mtype, req, field, overload, path, need)    \
{                                                                     \
	.magic = MAGIC_PARSER,                                        \
	.model = PARSER_MODEL_ARRAY_LINK_SIMPLE_FIELD,                \
	.ptr_offset = offsetof(stype, field),                         \
	.field_name = XSTRINGIFY(field),                              \
	.field_name_overloads = overload,                             \
	.key = path,                                                  \
	.required = req,                                              \
	.type = DATA_PARSER_ ## mtype,                                \
	.type_string = XSTRINGIFY(DATA_PARSER_ ## mtype),             \
	.obj_type_string = XSTRINGIFY(stype),                         \
	.flag = FLAG_TYPE_NONE,                                       \
	.flag_bit_array_count = NO_VAL8,                              \
	.size = sizeof(((stype *) NULL)->field),                      \
	.needs = need,                                                \
}
#define add_parser_skip(stype, field)                                 \
{                                                                     \
	.magic = MAGIC_PARSER,                                        \
	.model = PARSER_MODEL_ARRAY_SKIP_FIELD,                       \
	.ptr_offset = offsetof(stype, field),                         \
	.field_name = XSTRINGIFY(field),                              \
	.type = DATA_PARSER_TYPE_INVALID,                             \
	.obj_type_string = XSTRINGIFY(stype),                         \
	.flag = FLAG_TYPE_NONE,                                       \
	.flag_bit_array_count = NO_VAL8,                              \
	.size = sizeof(((stype *) NULL)->field),                      \
	.needs = NEED_NONE,                                           \
}
/*
 * Parser that needs the location of struct as
 * it will reference multiple fields at once.
 */
#define add_complex_parser(stype, mtype, req, path, need)             \
{                                                                     \
	.magic = MAGIC_PARSER,                                        \
	.model = PARSER_MODEL_ARRAY_LINK_COMPLEX_FIELD,               \
	.ptr_offset = NO_VAL,                                         \
	.key = path,                                                  \
	.required = req,                                              \
	.type = DATA_PARSER_ ## mtype,                                \
	.type_string = XSTRINGIFY(DATA_PARSER_ ## mtype),             \
	.obj_type_string = XSTRINGIFY(stype),                         \
	.flag = FLAG_TYPE_NONE,                                       \
	.flag_bit_array_count = NO_VAL8,                              \
	.size = NO_VAL,                                               \
	.needs = need                                                 \
}
/* will never set to FALSE, only will set to TRUE if matched  */
#define add_parse_bool(stype, mtype, req, field, path,           \
			    name, need)                               \
{                                                                     \
	.magic = MAGIC_PARSER,                                        \
	.model = PARSER_MODEL_ARRAY_BOOL_FIELD,                       \
	.ptr_offset = offsetof(stype, field),                         \
	.field_name = XSTRINGIFY(field),                              \
	.key = path,                                                  \
	.required = req,                                              \
	.type = DATA_PARSER_ ## mtype,                                \
	.type_string = XSTRINGIFY(DATA_PARSER_ ## mtype),             \
	.obj_type_string = XSTRINGIFY(stype),                         \
	.flag = FLAG_TYPE_BOOL,                                       \
	.flag_name = name,                                            \
	.flag_bit_array_count = NO_VAL8,                              \
	.size = sizeof(((stype *) NULL)->field),                      \
	.needs = need,                                                \
}
#define add_parse_bit_flag_array(stype, mtype, req, field, path)      \
{                                                                     \
	.magic = MAGIC_PARSER,                                        \
	.model = PARSER_MODEL_ARRAY_LINK_FLAGS_FIELD,                 \
	.ptr_offset = offsetof(stype, field),                         \
	.field_name = XSTRINGIFY(field),                              \
	.key = path,                                                  \
	.required = req,                                              \
	.type = DATA_PARSER_ ## mtype,                                \
	.type_string = XSTRINGIFY(DATA_PARSER_ ## mtype),             \
	.obj_type_string = XSTRINGIFY(stype),                         \
	.flag = FLAG_TYPE_BIT_ARRAY,                                  \
	.flag_name = NULL,                                            \
	.flag_bit_array = PARSER_FLAG_ARRAY(mtype),                   \
	.flag_bit_array_count = ARRAY_SIZE(PARSER_FLAG_ARRAY(mtype)), \
	.size = sizeof(((stype *) NULL)->field),                      \
	.needs = NEED_NONE,                                           \
}
#define add_flag_bit(flag_value, flag_string)                         \
	add_flag_masked_bit(flag_value, INFINITE64, flag_string)
#define add_flag_masked_bit(flag_value, flag_mask, flag_string)       \
	add_flag_bit_entry(FLAG_BIT_TYPE_BIT, #flag_value, flag_value,\
			   flag_mask, #flag_mask, flag_string)
#define add_flag_equal(flag_value, flag_mask, flag_string)            \
	add_flag_bit_entry(FLAG_BIT_TYPE_EQUAL, #flag_value,          \
			   flag_value, flag_mask, #flag_mask,         \
			   flag_string)
#define add_flag_bit_entry(flag_type, flag_value_string, flag_value,  \
			   flag_mask, flag_mask_string, flag_string)  \
{                                                                     \
	.magic = MAGIC_FLAG_BIT,                                      \
	.type = flag_type,                                            \
	.value = flag_value,                                          \
	.mask = flag_mask,                                            \
	.mask_size = sizeof(flag_mask),                               \
	.mask_name = flag_mask_string,                                \
	.name = flag_string,                                          \
	.flag_name = flag_value_string,                               \
	.flag_size = sizeof(flag_value),                              \
}

#define add_parse(mtype, field, path) \
	add_parser(slurmdb_assoc_rec_t, mtype, false, field, 0, path, NEED_NONE)
#define add_parse_req(mtype, field, path) \
	add_parser(slurmdb_assoc_rec_t, mtype, true, field, 0, path, NEED_NONE)
static const parser_t PARSER_ARRAY(ASSOC_SHORT)[] = {
	/* Identifiers required for any given association */
	add_parse_req(STRING, acct, "account"),
	add_parse(STRING, cluster, "cluster"),
	add_parse(STRING, partition, "partition"),
	add_parse_req(STRING, user, "user"),
};
#undef add_parse
#undef add_parse_req

static const flag_bit_t PARSER_FLAG_ARRAY(ASSOC_FLAGS)[] = {
	add_flag_bit(ASSOC_FLAG_DELETED, "DELETED"),
};

#define add_skip(field) \
	add_parser_skip(slurmdb_assoc_rec_t, field)
#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_assoc_rec_t, mtype, false, field, 0, path, needs)
#define add_parse_req(mtype, field, path, needs) \
	add_parser(slurmdb_assoc_rec_t, mtype, true, field, 0, path, needs)
/* should mirror the structure of slurmdb_assoc_rec_t */
static const parser_t PARSER_ARRAY(ASSOC)[] = {
	add_skip(accounting_list),
	add_parse_req(STRING, acct, "account", NEED_NONE),
	add_skip(assoc_next),
	add_skip(assoc_next_id),
	add_skip(bf_usage),
	add_parse(STRING, cluster, "cluster", NEED_NONE),
	add_parse(QOS_ID, def_qos_id, "default/qos", NEED_QOS),
	add_parse_bit_flag_array(slurmdb_assoc_rec_t, ASSOC_FLAGS, false, flags, "flags"),
	add_skip(lft),
	add_parse(UINT32, grp_jobs, "max/jobs/per/count", NEED_NONE),
	add_parse(UINT32, grp_jobs_accrue, "max/jobs/per/accruing", NEED_NONE),
	add_parse(UINT32, grp_submit_jobs, "max/jobs/per/submitted", NEED_NONE),
	add_parse(TRES_STR, grp_tres, "max/tres/total", NEED_TRES),
	add_parse(TRES_STR, max_tres_mins_pj, "max/tres/minutes/per/job", NEED_TRES),
	add_parse(TRES_STR, grp_tres_mins, "max/tres/group/minutes", NEED_TRES),
	add_skip(grp_tres_mins_ctld),
	add_parse(TRES_STR, grp_tres_run_mins, "max/tres/group/active", NEED_TRES),
	add_skip(id),
	add_parse_bool(slurmdb_assoc_rec_t, ASSOC_FLAG_DEFAULT, false, is_def, "is_default", "DEFAULT", NEED_NONE),
	add_parse(UINT32, max_jobs, "max/jobs/active", NEED_NONE),
	add_parse(UINT32, max_jobs_accrue, "max/jobs/accruing", NEED_NONE),
	add_parse(UINT32, max_submit_jobs, "max/jobs/total", NEED_NONE),
	add_skip(max_tres_mins_ctld),
	add_parse(TRES_STR, max_tres_run_mins, "max/tres/minutes/total", NEED_TRES),
	add_skip(grp_tres_run_mins_ctld),
	add_parse(UINT32, grp_wall, "max/per/account/wall_clock", NEED_NONE),
	add_parse(TRES_STR, max_tres_pj, "max/tres/per/job", NEED_TRES),
	add_skip(max_tres_ctld),
	add_parse(TRES_STR, max_tres_pn, "max/tres/per/node", NEED_TRES),
	add_skip(max_tres_pn_ctld),
	add_parse(UINT32, max_wall_pj, "max/jobs/per/wall_clock", NEED_NONE),
	add_parse(UINT32, min_prio_thresh, "min/priority_threshold", NEED_NONE),
	add_parse(STRING, parent_acct, "parent_account", NEED_NONE),
	add_skip(parent_id),
	add_parse(STRING, partition, "partition", NEED_NONE),
	add_parse(UINT32, priority, "priority", NEED_NONE),
	add_parse(QOS_STRING_ID_LIST, qos_list, "qos", NEED_QOS),
	add_skip(rgt),
	add_parse(UINT32, shares_raw, "shares_raw", NEED_NONE),
	/* slurmdbd should never set uid - it should always be zero */
	add_skip(uid),
	add_parse(ASSOC_USAGE_PTR, usage, "usage", NEED_NONE),
	add_parse_req(STRING, user, "user", NEED_NONE),
	add_skip(user_rec ),
};
#undef add_parse
#undef add_parse_req
#undef add_skip

static const flag_bit_t PARSER_FLAG_ARRAY(USER_FLAGS)[] = {
	add_flag_equal(SLURMDB_USER_FLAG_NONE, INFINITE64, "NONE"),
	add_flag_bit(SLURMDB_USER_FLAG_DELETED, "DELETED"),
};

#define add_skip(field) \
	add_parser_skip(slurmdb_user_rec_t, field)
#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_user_rec_t, mtype, false, field, 0, path, needs)
#define add_parse_req(mtype, field, path, needs) \
	add_parser(slurmdb_user_rec_t, mtype, true, field, 0, path, needs)
/* should mirror the structure of slurmdb_user_rec_t */
static const parser_t PARSER_ARRAY(USER)[] = {
	add_parse(ADMIN_LVL, admin_level, "administrator_level", NEED_NONE),
	add_parse(ASSOC_SHORT_LIST, assoc_list, "associations", NEED_ASSOC),
	add_parse(COORD_LIST, coord_accts, "coordinators", NEED_NONE),
	add_parse(STRING, default_acct, "default/account", NEED_NONE),
	add_parse(STRING, default_wckey, "default/wckey", NEED_NONE),
	add_parse_bit_flag_array(slurmdb_user_rec_t, USER_FLAGS, false, flags, "flags"),
	add_parse_req(STRING, name, "name", NEED_NONE),
	add_skip(old_name),
	/* uid should always be 0 */
	add_skip(uid),
};
#undef add_parse
#undef add_parse_req
#undef add_skip

static const flag_bit_t PARSER_FLAG_ARRAY(SLURMDB_JOB_FLAGS)[] = {
	add_flag_equal(SLURMDB_JOB_FLAG_NONE, INFINITE64, "NONE"),
	add_flag_bit(SLURMDB_JOB_CLEAR_SCHED, "CLEAR_SCHEDULING"),
	add_flag_bit(SLURMDB_JOB_FLAG_NOTSET, "NOT_SET"),
	add_flag_bit(SLURMDB_JOB_FLAG_SUBMIT, "STARTED_ON_SUBMIT"),
	add_flag_bit(SLURMDB_JOB_FLAG_SCHED, "STARTED_ON_SCHEDULE"),
	add_flag_bit(SLURMDB_JOB_FLAG_BACKFILL, "STARTED_ON_BACKFILL"),
};

#define add_skip(field) \
	add_parser_skip(slurmdb_job_rec_t, field)
#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_job_rec_t, mtype, false, field, 0, path, needs)
/* should mirror the structure of slurmdb_job_rec_t  */
static const parser_t PARSER_ARRAY(JOB)[] = {
	add_parse(STRING, account, "account", NEED_NONE),
	add_parse(STRING, admin_comment, "comment/administrator", NEED_NONE),
	add_parse(UINT32, alloc_nodes, "allocation_nodes", NEED_NONE),
	add_parse(UINT32, array_job_id, "array/job_id", NEED_NONE),
	add_parse(UINT32, array_max_tasks, "array/limits/max/running/tasks", NEED_NONE),
	add_parse(UINT32_NO_VAL, array_task_id, "array/task_id", NEED_NONE),
	add_parse(STRING, array_task_str, "array/task", NEED_NONE),
	add_parse(ASSOC_ID, associd, "association", NEED_ASSOC),
	add_parse(STRING, blockid, "block", NEED_NONE),
	add_parse(STRING, cluster, "cluster", NEED_NONE),
	add_parse(STRING, constraints, "constraints", NEED_NONE),
	add_parse(STRING, container, "container", NEED_NONE),
	add_skip(db_index),
	add_parse(JOB_EXIT_CODE, derived_ec, "derived_exit_code", NEED_NONE),
	add_parse(STRING, derived_es, "comment/job", NEED_NONE),
	add_parse(UINT32, elapsed, "time/elapsed", NEED_NONE),
	add_parse(UINT64, eligible, "time/eligible", NEED_NONE),
	add_parse(UINT64, end, "time/end", NEED_NONE),
	add_skip(env),
	add_parse(JOB_EXIT_CODE, exitcode, "exit_code", NEED_NONE),
	add_parse(STRING, extra, "extra", NEED_NONE),
	add_parse(STRING, failed_node, "failed_node", NEED_NONE),
	add_parse_bit_flag_array(slurmdb_job_rec_t, SLURMDB_JOB_FLAGS, false, flags, "flags"),
	add_skip(first_step_ptr),
	add_parse(GROUP_ID, gid, "group", NEED_NONE),
	add_parse(UINT32, het_job_id, "het/job_id", NEED_NONE),
	add_parse(UINT32_NO_VAL, het_job_offset, "het/job_offset", NEED_NONE),
	add_parse(UINT32, jobid, "job_id", NEED_NONE),
	add_parse(STRING, jobname, "name", NEED_NONE),
	add_skip(lft),
	add_parse(STRING, licenses, "licenses", NEED_NONE),
	add_parse(STRING, mcs_label, "mcs/label", NEED_NONE),
	add_parse(STRING, nodes, "nodes", NEED_NONE),
	add_parse(STRING, partition, "partition", NEED_NONE),
	add_parse(UINT32_NO_VAL, priority, "priority", NEED_NONE),
	add_parse(QOS_ID, qosid, "qos", NEED_QOS),
	add_parse(UINT32, req_cpus, "required/CPUs", NEED_NONE),
	add_parse(UINT64, req_mem, "required/memory", NEED_NONE),
	add_parse(USER_ID, requid, "kill_request_user", NEED_NONE),
	add_parse(UINT32, resvid, "reservation/id", NEED_NONE),
	add_parse(STRING, resv_name, "reservation/name", NEED_NONE),
	add_parse(STRING, script, "script", NEED_NONE),
	add_skip(show_full),
	add_parse(UINT64, start, "time/start", NEED_NONE),
	add_parse(JOB_STATE, state, "state/current", NEED_NONE),
	add_parse(JOB_REASON, state_reason_prev, "state/reason", NEED_NONE),
	add_parse(STEP_LIST, steps, "steps", NEED_NONE),
	add_parse(UINT64, submit, "time/submission", NEED_NONE),
	add_parse(STRING, submit_line, "submit_line", NEED_NONE),
	add_parse(UINT32, suspended, "time/suspended", NEED_NONE),
	add_parse(STRING, system_comment, "comment/system", NEED_NONE),
	add_parse(UINT64, sys_cpu_sec, "time/system/seconds", NEED_NONE),
	add_parse(UINT64, sys_cpu_usec, "time/system/microseconds", NEED_NONE),
	add_parse(UINT32_NO_VAL, timelimit, "time/limit", NEED_NONE),
	add_parse(UINT64, tot_cpu_sec, "time/total/seconds", NEED_NONE),
	add_parse(UINT64, tot_cpu_usec, "time/total/microseconds", NEED_NONE),
	add_parse(TRES_STR, tres_alloc_str, "tres/allocated", NEED_TRES),
	add_parse(TRES_STR, tres_req_str, "tres/requested", NEED_TRES),
	add_skip(uid), /* dup with complex parser JOB_USER  below */
	add_parse(STRING, used_gres, "used_gres", NEED_NONE),
	add_skip(user), /* dup with complex parser JOB_USER below */
	/* parse uid or user depending on which is available */
	add_complex_parser(slurmdb_job_rec_t, JOB_USER, false, "user", NEED_NONE),
	add_parse(UINT64, user_cpu_sec, "time/user/seconds", NEED_NONE),
	add_parse(UINT64, user_cpu_usec, "time/user/microseconds", NEED_NONE),
	add_parse(WCKEY_TAG, wckey, "wckey", NEED_NONE),
	add_skip(wckeyid),
	add_parse(STRING, work_dir, "working_directory", NEED_NONE),
};
#undef add_parse
#undef add_skip

static const flag_bit_t PARSER_FLAG_ARRAY(ACCOUNT_FLAGS)[] = {
	add_flag_bit(SLURMDB_ACCT_FLAG_DELETED, "DELETED"),
};

#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_account_rec_t, mtype, false, field, 0, path, needs)
/* should mirror the structure of slurmdb_account_rec_t */
static const parser_t PARSER_ARRAY(ACCOUNT)[] = {
	add_parse(ASSOC_SHORT_LIST, assoc_list, "associations", NEED_ASSOC),
	add_parse(COORD_LIST, coordinators, "coordinators", NEED_NONE),
	add_parse(STRING, description, "description", NEED_NONE),
	add_parse(STRING, name, "name", NEED_NONE),
	add_parse(STRING, organization, "organization", NEED_NONE),
	add_parse_bit_flag_array(slurmdb_account_rec_t, ACCOUNT_FLAGS, false, flags, "flags"),
};
#undef add_parse

#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_accounting_rec_t, mtype, false, field, 0, path, needs)
/* should mirror the structure of slurmdb_accounting_rec_t */
static const parser_t PARSER_ARRAY(ACCOUNTING)[] = {
	add_parse(UINT64, alloc_secs, "allocated/seconds", NEED_NONE),
	add_parse(UINT32, id, "id", NEED_NONE),
	add_parse(UINT64, period_start, "start", NEED_NONE),
	add_parse(TRES, tres_rec, "TRES", NEED_TRES),
};
#undef add_parse

#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_coord_rec_t, mtype, false, field, 0, path, needs)
#define add_parse_req(mtype, field, path, needs) \
	add_parser(slurmdb_coord_rec_t, mtype, true, field, 0, path, needs)
/* should mirror the structure of slurmdb_coord_rec_t  */
static const parser_t PARSER_ARRAY(COORD)[] = {
	add_parse_req(STRING, name, "name", NEED_NONE),
	add_parse(BOOL16, direct, "direct", NEED_NONE),
};
#undef add_parse
#undef add_parse_req

static const flag_bit_t PARSER_FLAG_ARRAY(WCKEY_FLAGS)[] = {
	add_flag_bit(SLURMDB_WCKEY_FLAG_DELETED, "DELETED"),
};

#define add_skip(field) \
	add_parser_skip(slurmdb_wckey_rec_t, field)
#define add_parse(mtype, field, path) \
	add_parser(slurmdb_wckey_rec_t, mtype, false, field, 0, path, NEED_NONE)
#define add_parse_req(mtype, field, path) \
	add_parser(slurmdb_wckey_rec_t, mtype, true, field, 0, path, NEED_NONE)
/* should mirror the structure of slurmdb_wckey_rec_t */
static const parser_t PARSER_ARRAY(WCKEY)[] = {
	add_parse(ACCOUNTING_LIST, accounting_list, "accounting"),
	add_parse_req(STRING, cluster, "cluster"),
	add_parse(UINT32, id, "id"),
	add_parse_req(STRING, name, "name"),
	add_parse_req(STRING, user, "user"),
	add_skip(uid),
	add_parse_bit_flag_array(slurmdb_wckey_rec_t, WCKEY_FLAGS, false, flags, "flags"),
};
#undef add_parse
#undef add_parse_req
#undef add_skip

#define add_skip(field) \
	add_parser_skip(slurmdb_tres_rec_t, field)
#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_tres_rec_t, mtype, false, field, 0, path, needs)
#define add_parse_req(mtype, field, path, needs) \
	add_parser(slurmdb_tres_rec_t, mtype, true, field, 0, path, needs)
/* should mirror the structure of slurmdb_tres_rec_t  */
static const parser_t PARSER_ARRAY(TRES)[] = {
	add_skip(alloc_secs), /* sreport func */
	add_skip(rec_count), /* not packed */
	add_parse_req(STRING, type, "type", NEED_NONE),
	add_parse(STRING, name, "name", NEED_NONE),
	add_parse(UINT32, id, "id", NEED_NONE),
	add_parse(INT64, count, "count", NEED_NONE),
};
#undef add_parse
#undef add_parse_req
#undef add_skip

static const flag_bit_t PARSER_FLAG_ARRAY(QOS_FLAGS)[] = {
	add_flag_masked_bit(QOS_FLAG_NOTSET, ~QOS_FLAG_BASE, "NOT_SET"),
	add_flag_masked_bit(QOS_FLAG_ADD, ~QOS_FLAG_BASE, "ADD"),
	add_flag_masked_bit(QOS_FLAG_REMOVE, ~QOS_FLAG_BASE, "REMOVE"),
	add_flag_masked_bit(QOS_FLAG_PART_MIN_NODE, QOS_FLAG_BASE, "PARTITION_MINIMUM_NODE"),
	add_flag_masked_bit(QOS_FLAG_PART_MAX_NODE, QOS_FLAG_BASE, "PARTITION_MAXIMUM_NODE"),
	add_flag_masked_bit(QOS_FLAG_PART_TIME_LIMIT, QOS_FLAG_BASE, "PARTITION_TIME_LIMIT"),
	add_flag_masked_bit(QOS_FLAG_ENFORCE_USAGE_THRES, QOS_FLAG_BASE, "ENFORCE_USAGE_THRESHOLD"),
	add_flag_masked_bit(QOS_FLAG_NO_RESERVE, QOS_FLAG_BASE, "NO_RESERVE"),
	add_flag_masked_bit(QOS_FLAG_REQ_RESV, QOS_FLAG_BASE, "REQUIRED_RESERVATION"),
	add_flag_masked_bit(QOS_FLAG_DENY_LIMIT, QOS_FLAG_BASE, "DENY_LIMIT"),
	add_flag_masked_bit(QOS_FLAG_OVER_PART_QOS, QOS_FLAG_BASE, "OVERRIDE_PARTITION_QOS"),
	add_flag_masked_bit(QOS_FLAG_NO_DECAY, QOS_FLAG_BASE, "NO_DECAY"),
	add_flag_masked_bit(QOS_FLAG_USAGE_FACTOR_SAFE, QOS_FLAG_BASE, "USAGE_FACTOR_SAFE"),
};

static const flag_bit_t PARSER_FLAG_ARRAY(QOS_PREEMPT_MODES)[] = {
	add_flag_equal(PREEMPT_MODE_OFF, INFINITE64, "DISABLED"),
	add_flag_bit(PREEMPT_MODE_SUSPEND, "SUSPEND"),
	add_flag_bit(PREEMPT_MODE_REQUEUE, "REQUEUE"),
	add_flag_bit(PREEMPT_MODE_CANCEL, "CANCEL"),
	add_flag_bit(PREEMPT_MODE_GANG, "GANG"),
};

#define add_skip(field) \
	add_parser_skip(slurmdb_qos_rec_t, field)
#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_qos_rec_t, mtype, false, field, 0, path, needs)
#define add_parse_req(mtype, field, path, needs) \
	add_parser(slurmdb_qos_rec_t, mtype, true, field, 0, path, needs)
/* should mirror the structure of slurmdb_qos_rec_t */
static const parser_t PARSER_ARRAY(QOS)[] = {
	add_parse(STRING, description, "description", NEED_NONE),
	add_parse_bit_flag_array(slurmdb_qos_rec_t, QOS_FLAGS, false, flags, "flags"),
	add_parse(UINT32, id, "id", NEED_NONE),
	add_parse(UINT32, grace_time, "limits/grace_time", NEED_NONE),
	add_parse(UINT32, grp_jobs_accrue, "limits/max/active_jobs/accruing", NEED_NONE),
	add_parse(UINT32, grp_jobs, "limits/max/active_jobs/count", NEED_NONE),
	add_parse(TRES_STR, grp_tres, "limits/max/tres/total", NEED_TRES),
	add_skip(grp_tres_ctld), /* not packed */
	add_parse(TRES_STR, grp_tres_run_mins, "limits/max/tres/minutes/per/qos", NEED_TRES),
	add_skip(grp_tres_run_mins_ctld), /* not packed */
	add_parse(STRING, name, "name", NEED_NONE),
	add_parse(UINT32, grp_wall, "limits/max/wall_clock/per/qos", NEED_NONE),
	add_parse(FLOAT64, limit_factor, "limits/factor", NEED_NONE),
	add_parse(UINT32, max_jobs_pa, "limits/max/jobs/active_jobs/per/account", NEED_NONE),
	add_parse(UINT32, max_jobs_pu, "limits/max/jobs/active_jobs/per/user", NEED_NONE),
	add_parse(UINT32, max_jobs_accrue_pa, "limits/max/accruing/per/account", NEED_NONE),
	add_parse(UINT32, max_jobs_accrue_pu, "limits/max/accruing/per/user", NEED_NONE),
	add_parse(UINT32, max_submit_jobs_pa, "limits/max/jobs/per/account", NEED_NONE),
	add_parse(UINT32, max_submit_jobs_pu, "limits/max/jobs/per/user", NEED_NONE),
	add_parse(TRES_STR, max_tres_mins_pj, "limits/max/tres/minutes/per/job", NEED_TRES),
	add_skip(max_tres_mins_pj_ctld), /* not packed */
	add_parse(TRES_STR, max_tres_pa, "limits/max/tres/per/account", NEED_TRES),
	add_skip(max_tres_pa_ctld), /* not packed */
	add_parse(TRES_STR, max_tres_pj, "limits/max/tres/per/job", NEED_TRES),
	add_skip(max_tres_pj_ctld), /* not packed */
	add_parse(TRES_STR, max_tres_pn, "limits/max/tres/per/node", NEED_TRES),
	add_skip(max_tres_pn_ctld), /* not packed */
	add_parse(TRES_STR, max_tres_pu, "limits/max/tres/per/user", NEED_TRES),
	add_skip(max_tres_pu_ctld), /* not packed */
	add_parse(TRES_STR, max_tres_run_mins_pa, "limits/max/tres/minutes/per/account", NEED_TRES),
	add_skip(max_tres_run_mins_pa_ctld), /* not packed */
	add_parse(TRES_STR, max_tres_run_mins_pu, "limits/max/tres/minutes/per/user", NEED_TRES),
	add_skip(max_tres_run_mins_pu_ctld), /* not packed */
	add_parse(UINT32, max_wall_pj, "limits/max/wall_clock/per/job", NEED_NONE),
	add_parse(UINT32, min_prio_thresh, "limits/min/priority_threshold", NEED_NONE),
	add_parse(TRES_STR, min_tres_pj, "limits/min/tres/per/job", NEED_NONE),
	add_skip(min_tres_pj_ctld), /* not packed */
	add_complex_parser(slurmdb_qos_rec_t, QOS_PREEMPT_LIST, false, "preempt/list", NEED_QOS),
	add_parse_bit_flag_array(slurmdb_qos_rec_t, QOS_PREEMPT_MODES, false, preempt_mode, "preempt/mode"),
	add_parse(UINT32, preempt_exempt_time, "preempt/exempt_time", NEED_NONE),
	add_parse(UINT32, priority, "priority", NEED_NONE),
	add_skip(usage), /* not packed */
	add_parse(FLOAT64, usage_factor, "usage_factor", NEED_NONE),
	add_parse(FLOAT64, usage_thres, "usage_threshold", NEED_NONE),
	add_skip(blocked_until), /* not packed */
};
#undef add_parse
#undef add_parse_req
#undef add_skip
#undef add_skip_flag

#define add_skip(field) \
	add_parser_skip(slurmdb_step_rec_t, field)
#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_step_rec_t, mtype, false, field, 0, path, needs)
/* should mirror the structure of slurmdb_step_rec_t */
static const parser_t PARSER_ARRAY(STEP)[] = {
	add_parse(UINT32, elapsed, "time/elapsed", NEED_NONE),
	add_parse(UINT64, end, "time/end", NEED_NONE),
	add_parse(JOB_EXIT_CODE, exitcode, "exit_code", NEED_NONE),
	add_skip(job_ptr), /* redundant here */
	add_parse(UINT32, nnodes, "nodes/count", NEED_NONE),
	add_parse(STRING, nodes, "nodes/range", NEED_NONE),
	add_parse(UINT32, ntasks, "tasks/count", NEED_NONE),
	add_parse(STRING, pid_str, "pid", NEED_NONE),
	add_parse(UINT32_NO_VAL, req_cpufreq_min, "CPU/requested_frequency/min", NEED_NONE),
	add_parse(UINT32_NO_VAL, req_cpufreq_max, "CPU/requested_frequency/max", NEED_NONE),
	add_parse(CPU_FREQ_FLAGS, req_cpufreq_gov, "CPU/governor", NEED_NONE),
	add_parse(USER_ID, requid, "kill_request_user", NEED_NONE),
	add_parse(UINT64, start, "time/start", NEED_NONE),
	add_parse(JOB_STATE, state, "state", NEED_NONE),
	add_parse(UINT64, stats.act_cpufreq, "statistics/CPU/actual_frequency", NEED_NONE),
	add_parse(UINT64, stats.consumed_energy, "statistics/energy/consumed", NEED_NONE),
	add_parse(SLURM_STEP_ID, step_id, "step/id", NEED_NONE),
	add_parse(STRING, stepname, "step/name", NEED_NONE),
	add_parse(UINT32, suspended, "time/suspended", NEED_NONE),
	add_parse(UINT64, sys_cpu_sec, "time/system/seconds", NEED_NONE),
	add_parse(UINT32, sys_cpu_usec, "time/system/microseconds", NEED_NONE),
	add_parse(TASK_DISTRIBUTION, task_dist, "task/distribution", NEED_NONE),
	add_parse(UINT64, tot_cpu_sec, "time/total/seconds", NEED_NONE),
	add_parse(UINT32, tot_cpu_usec, "time/total/microseconds", NEED_NONE),
	add_parse(UINT64, user_cpu_sec, "time/user/seconds", NEED_NONE),
	add_parse(UINT32, user_cpu_usec, "time/user/microseconds", NEED_NONE),
	add_complex_parser(slurmdb_step_rec_t, STEP_NODES, false, "nodes/list", NEED_NONE),
	add_complex_parser(slurmdb_step_rec_t, STEP_TRES_REQ_MAX, false, "tres/requested/max", NEED_TRES),
	add_complex_parser(slurmdb_step_rec_t, STEP_TRES_REQ_MIN, false, "tres/requested/min", NEED_TRES),
	add_complex_parser(slurmdb_step_rec_t, STEP_TRES_USAGE_MAX, false, "tres/consumed/max", NEED_TRES),
	add_complex_parser(slurmdb_step_rec_t, STEP_TRES_USAGE_MIN, false, "tres/consumed/min", NEED_TRES),
	add_parse(TRES_STR, stats.tres_usage_in_ave, "tres/requested/average", NEED_TRES),
	add_parse(TRES_STR, stats.tres_usage_in_tot, "tres/requested/total", NEED_TRES),
	add_parse(TRES_STR, stats.tres_usage_out_ave, "tres/consumed/average", NEED_TRES),
	add_parse(TRES_STR, stats.tres_usage_out_tot, "tres/consumed/total", NEED_TRES),
	add_parse(TRES_STR, tres_alloc_str, "tres/allocated", NEED_TRES),
};
#undef add_parse
#undef add_skip

#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_stats_rec_t, mtype, false, field, 0, path, needs)
/* should mirror the structure of slurmdb_stats_rec_t */
static const parser_t PARSER_ARRAY(STATS_REC)[] = {
	add_parse(UINT64, time_start, "time_start", NEED_NONE),
	add_parse(STATS_REC_ARRAY_PTR, dbd_rollup_stats, "rollups", NEED_NONE),
	add_parse(STATS_RPC_LIST, rpc_list, "RPCs", NEED_NONE),
	add_parse(STATS_USER_LIST, user_list, "users", NEED_NONE),
};
#undef add_parse

#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_rpc_obj_t, mtype, false, field, 0, path, needs)
/* should mirror the structure of slurmdb_rpc_obj_t */
static const parser_t PARSER_ARRAY(STATS_USER)[] = {
	add_parse(USER_ID, id, "user", NEED_NONE),
	add_parse(UINT32, cnt, "count", NEED_NONE),
	add_parse(UINT64, time_ave, "time/average", NEED_NONE),
	add_parse(UINT64, time, "time/total", NEED_NONE),
};
#undef add_parse

#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_rpc_obj_t, mtype, false, field, 0, path, needs)
/* should mirror the structure of slurmdb_rpc_obj_t */
static const parser_t PARSER_ARRAY(STATS_RPC)[] = {
	add_parse(RPC_ID, id, "rpc", NEED_NONE),
	add_parse(UINT32, cnt, "count", NEED_NONE),
	add_parse(UINT64, time_ave, "time/average", NEED_NONE),
	add_parse(UINT64, time, "time/total", NEED_NONE),
};
#undef add_parse

static const flag_bit_t PARSER_FLAG_ARRAY(CLUSTER_REC_FLAGS)[] = {
	add_flag_bit(CLUSTER_FLAG_REGISTER, "REGISTERING"),
	add_flag_bit(CLUSTER_FLAG_MULTSD, "MULTIPLE_SLURMD"),
	add_flag_bit(CLUSTER_FLAG_FE, "FRONT_END"),
	add_flag_bit(CLUSTER_FLAG_CRAY, "CRAY_NATIVE"),
	add_flag_bit(CLUSTER_FLAG_FED, "FEDERATION"),
	add_flag_bit(CLUSTER_FLAG_EXT, "EXTERNAL"),
};

#define add_skip(field) \
	add_parser_skip(slurmdb_cluster_rec_t, field)
#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_cluster_rec_t, mtype, false, field, 0, path, needs)
/* should mirror the structure of slurmdb_cluster_rec_t */
static const parser_t PARSER_ARRAY(CLUSTER_REC)[] = {
	add_skip(classification), /* to be deprecated */
	add_skip(comm_fail_time), /* not packed */
	add_skip(control_addr), /* not packed */
	add_parse(STRING, control_host, "controller/host", NEED_NONE),
	add_parse(UINT32, control_port, "controller/port", NEED_NONE),
	add_skip(dim_size), /* BG deprecated */
	add_skip(fed), /* federation not supportted */
	add_parse_bit_flag_array(slurmdb_cluster_rec_t, CLUSTER_REC_FLAGS, false, flags, "flags"),
	add_skip(lock), /* not packed */
	add_parse(STRING, name, "name", NEED_NONE),
	add_parse(STRING, nodes, "nodes", NEED_NONE),
	add_parse(SELECT_PLUGIN_ID, plugin_id_select, "select_plugin", NEED_NONE),
	add_parse(ASSOC_SHORT_PTR, root_assoc, "associations/root", NEED_ASSOC),
	add_parse(UINT16, rpc_version, "rpc_version", NEED_NONE),
	add_skip(send_rpc), /* not packed */
	add_parse(TRES_STR, tres_str, "tres", NEED_TRES),
};
#undef add_parse
#undef add_skip

#define add_parse(mtype, field, path) \
	add_parser(slurmdb_cluster_accounting_rec_t, mtype, false, field, 0, path, NEED_NONE)
/* should mirror the structure of slurmdb_cluster_accounting_rec_t */
static const parser_t PARSER_ARRAY(CLUSTER_ACCT_REC)[] = {
	add_parse(UINT64, alloc_secs, "time/allocated"),
	add_parse(UINT64, down_secs, "time/down"),
	add_parse(UINT64, idle_secs, "time/idle"),
	add_parse(UINT64, over_secs, "time/overcommitted"),
	add_parse(UINT64, pdown_secs, "time/planned_down"),
	add_parse(UINT64, period_start, "time/start"),
	add_parse(STRING, tres_rec.name, "tres/name"),
	add_parse(STRING, tres_rec.type, "tres/type"),
	add_parse(UINT32, tres_rec.id, "tres/id"),
	add_parse(UINT64, tres_rec.count, "tres/count"),
};
#undef add_parse

#define add_parse(mtype, field, path) \
	add_parser(slurmdb_tres_nct_rec_t, mtype, false, field, 0, path, NEED_NONE)
#define add_parse_req(mtype, field, path) \
	add_parser(slurmdb_tres_nct_rec_t, mtype, true, field, 0, path, NEED_NONE)
/* should mirror the structure of slurmdb_tres_nct_rec_t  */
static const parser_t PARSER_ARRAY(TRES_NCT)[] = {
	add_parse_req(STRING, type, "type"),
	add_parse(STRING, name, "name"),
	add_parse(UINT32, id, "id"),
	add_parse(INT64, count, "count"),
	add_parse(INT64, task, "task"),
	add_parse(STRING, node, "node"),
};
#undef add_parse
#undef add_parse_req

#define add_skip(field) \
	add_parser_skip(slurmdb_assoc_usage_t, field)
#define add_parse(mtype, field, path) \
	add_parser(slurmdb_assoc_usage_t, mtype, false, field, 0, path, NEED_NONE)
/* should mirror the structure of slurmdb_assoc_usage_t */
static const parser_t PARSER_ARRAY(ASSOC_USAGE)[] = {
	add_parse(UINT32, accrue_cnt, "accrue_job_count"),
	add_skip(children_list), /* not packed */
	add_skip(grp_node_bitmap), /* not packed */
	add_skip(grp_node_job_cnt), /* not packed */
	add_skip(grp_used_tres), /* not packed */
	add_skip(grp_used_tres_run_secs), /* not packed */
	add_parse(FLOAT64, grp_used_wall, "group_used_wallclock"),
	add_parse(FLOAT64, fs_factor, "fairshare_factor"),
	add_parse(UINT32, level_shares, "fairshare_shares"),
	add_skip(parent_assoc_ptr), /* not packed */
	add_parse(FLOAT64, priority_norm, "normalized_priority"),
	add_skip(fs_assoc_ptr), /* not packed */
	add_parse(FLOAT64, shares_norm, "normalized_shares"),
	add_parse(FLOAT128, usage_efctv, "effective_normalized_usage"),
	add_parse(FLOAT128, usage_norm, "normalized_usage"),
	add_parse(FLOAT128, usage_raw, "raw_usage"),
	add_parse(UINT32, used_jobs, "active_jobs"),
	add_parse(UINT32, used_submit_jobs, "job_count"),
	add_parse(FLOAT128, level_fs, "fairshare_level"),
	add_skip(valid_qos),
};
#undef add_parse
#undef add_skip

#define add_skip(field) \
	add_parser_skip(stats_info_response_msg_t, field)
#define add_parse(mtype, field, path) \
	add_parser(stats_info_response_msg_t, mtype, false, field, 0, path, NEED_NONE)
#define add_cparse(mtype, path) \
	add_complex_parser(stats_info_response_msg_t, mtype, false, path, NEED_NONE)
static const parser_t PARSER_ARRAY(STATS_MSG)[] = {
	add_parse(UINT32, parts_packed, "parts_packed"),
	add_parse(INT64, req_time, "req_time"),
	add_parse(INT64, req_time_start, "req_time_start"),
	add_parse(UINT32, server_thread_count, "server_thread_count"),
	add_parse(UINT32, agent_queue_size, "agent_queue_size"),
	add_parse(UINT32, agent_count, "agent_count"),
	add_parse(UINT32, agent_thread_count, "agent_thread_count"),
	add_parse(UINT32, dbd_agent_queue_size, "dbd_agent_queue_size"),
	add_parse(UINT32, gettimeofday_latency, "gettimeofday_latency"),
	add_parse(UINT32, schedule_cycle_max, "schedule_cycle_max"),
	add_parse(UINT32, schedule_cycle_last, "schedule_cycle_last"),
	add_skip(schedule_cycle_sum),
	add_parse(UINT32, schedule_cycle_counter, "schedule_cycle_total"),
	add_cparse(STATS_MSG_CYCLE_MEAN, "schedule_cycle_mean"),
	add_cparse(STATS_MSG_CYCLE_MEAN_DEPTH, "schedule_cycle_mean_depth"),
	add_cparse(STATS_MSG_CYCLE_PER_MIN, "schedule_cycle_per_minute"),
	add_skip(schedule_cycle_depth),
	add_parse(UINT32, schedule_queue_len, "schedule_queue_length"),
	add_parse(UINT32, jobs_submitted, "jobs_submitted"),
	add_parse(UINT32, jobs_started, "jobs_started"),
	add_parse(UINT32, jobs_completed, "jobs_completed"),
	add_parse(UINT32, jobs_canceled, "jobs_canceled"),
	add_parse(UINT32, jobs_failed, "jobs_failed"),
	add_parse(UINT32, jobs_pending, "jobs_pending"),
	add_parse(UINT32, jobs_running, "jobs_running"),
	add_parse(INT64, job_states_ts, "job_states_ts"),
	add_parse(UINT32, bf_backfilled_jobs, "bf_backfilled_jobs"),
	add_parse(UINT32, bf_last_backfilled_jobs, "bf_last_backfilled_jobs"),
	add_parse(UINT32, bf_backfilled_het_jobs, "bf_backfilled_het_jobs"),
	add_parse(UINT32, bf_cycle_counter, "bf_cycle_counter"),
	add_cparse(STATS_MSG_BF_CYCLE_MEAN, "bf_cycle_mean"),
	add_cparse(STATS_MSG_BF_DEPTH_MEAN, "bf_depth_mean"),
	add_cparse(STATS_MSG_BF_DEPTH_MEAN_TRY, "bf_depth_mean_try"),
	add_parse(UINT64, bf_cycle_sum, "bf_cycle_sum"),
	add_parse(UINT32, bf_cycle_last, "bf_cycle_last"),
	add_parse(UINT32, bf_last_depth, "bf_last_depth"),
	add_parse(UINT32, bf_last_depth_try, "bf_last_depth_try"),
	add_parse(UINT32, bf_depth_sum, "bf_depth_sum"),
	add_parse(UINT32, bf_depth_try_sum, "bf_depth_try_sum"),
	add_parse(UINT32, bf_queue_len, "bf_queue_len"),
	add_cparse(STATS_MSG_BF_QUEUE_LEN_MEAN, "bf_queue_len_mean"),
	add_parse(UINT32, bf_queue_len_sum, "bf_queue_len_sum"),
	add_parse(UINT32, bf_table_size, "bf_table_size"),
	add_skip(bf_table_size_sum),
	add_cparse(STATS_MSG_BF_QUEUE_LEN_MEAN, "bf_table_size_mean"),
	add_parse(INT64, bf_when_last_cycle, "bf_when_last_cycle"),
	add_cparse(STATS_MSG_BF_ACTIVE, "bf_active"),
	add_skip(rpc_type_size),
	add_cparse(STATS_MSG_RPCS_BY_TYPE, "rpcs_by_message_type"),
	add_skip(rpc_type_id), /* handled by STATS_MSG_RPCS_BY_TYPE */
	add_skip(rpc_type_cnt), /* handled by STATS_MSG_RPCS_BY_TYPE */
	add_skip(rpc_type_time), /* handled by STATS_MSG_RPCS_BY_TYPE */
	add_cparse(STATS_MSG_RPCS_BY_USER, "rpcs_by_user"),
	add_skip(rpc_user_size), /* handled by STATS_MSG_RPCS_BY_TYPE */
	add_skip(rpc_user_id), /* handled by STATS_MSG_RPCS_BY_TYPE */
	add_skip(rpc_user_cnt), /* handled by STATS_MSG_RPCS_BY_TYPE */
	add_skip(rpc_user_time), /* handled by STATS_MSG_RPCS_BY_TYPE */
	add_skip(rpc_queue_type_count), /* TODO: implement */
	add_skip(rpc_queue_type_id ), /* TODO: implement */
	add_skip(rpc_queue_count), /* TODO: implement */
	add_skip(rpc_dump_count), /* TODO: implement */
	add_skip(rpc_dump_types), /* TODO: implement */
	add_skip(rpc_dump_hostlist), /* TODO: implement */
};
#undef add_parse
#undef add_cparse
#undef add_skip

static const flag_bit_t PARSER_FLAG_ARRAY(NODE_STATES)[] = {
	add_flag_equal(NODE_STATE_UNKNOWN, NODE_STATE_BASE, "UNKNOWN"),
	add_flag_equal(NODE_STATE_DOWN, NODE_STATE_BASE, "DOWN"),
	add_flag_equal(NODE_STATE_IDLE, NODE_STATE_BASE, "IDLE"),
	add_flag_equal(NODE_STATE_ALLOCATED, NODE_STATE_BASE, "ALLOCATED"),
	add_flag_equal(NODE_STATE_ERROR, NODE_STATE_BASE, "ERROR"),
	add_flag_equal(NODE_STATE_MIXED, NODE_STATE_BASE, "MIXED"),
	add_flag_equal(NODE_STATE_FUTURE, NODE_STATE_BASE, "FUTURE"),
	add_flag_masked_bit(NODE_STATE_NET, NODE_STATE_FLAGS, "PERFCTRS"),
	add_flag_masked_bit(NODE_STATE_RES, NODE_STATE_FLAGS, "RESERVED"),
	add_flag_masked_bit(NODE_STATE_UNDRAIN, NODE_STATE_FLAGS, "UNDRAIN"),
	add_flag_masked_bit(NODE_STATE_CLOUD, NODE_STATE_FLAGS, "CLOUD"),
	add_flag_masked_bit(NODE_RESUME, NODE_STATE_FLAGS, "RESUME"),
	add_flag_masked_bit(NODE_STATE_DRAIN, NODE_STATE_FLAGS, "DRAIN"),
	add_flag_masked_bit(NODE_STATE_COMPLETING, NODE_STATE_FLAGS, "COMPLETING"),
	add_flag_masked_bit(NODE_STATE_NO_RESPOND, NODE_STATE_FLAGS, "NOT_RESPONDING"),
	add_flag_masked_bit(NODE_STATE_POWERED_DOWN, NODE_STATE_FLAGS, "POWERED_DOWN"),
	add_flag_masked_bit(NODE_STATE_FAIL, NODE_STATE_FLAGS, "FAIL"),
	add_flag_masked_bit(NODE_STATE_POWERING_UP, NODE_STATE_FLAGS, "POWERING_UP"),
	add_flag_masked_bit(NODE_STATE_MAINT, NODE_STATE_FLAGS, "MAINTENANCE"),
	add_flag_masked_bit(NODE_STATE_REBOOT_REQUESTED, NODE_STATE_FLAGS, "REBOOT_REQUESTED"),
	add_flag_masked_bit(NODE_STATE_REBOOT_CANCEL, NODE_STATE_FLAGS, "REBOOT_CANCELED"),
	add_flag_masked_bit(NODE_STATE_POWERING_DOWN, NODE_STATE_FLAGS, "POWERING_DOWN"),
	add_flag_masked_bit(NODE_STATE_DYNAMIC_FUTURE, NODE_STATE_FLAGS, "DYNAMIC_FUTURE"),
	add_flag_masked_bit(NODE_STATE_REBOOT_ISSUED, NODE_STATE_FLAGS, "REBOOT_ISSUED"),
	add_flag_masked_bit(NODE_STATE_PLANNED, NODE_STATE_FLAGS, "PLANNED"),
	add_flag_masked_bit(NODE_STATE_INVALID_REG, NODE_STATE_FLAGS, "INVALID_REG"),
	add_flag_masked_bit(NODE_STATE_POWER_DOWN, NODE_STATE_FLAGS, "POWER_DOWN"),
	add_flag_masked_bit(NODE_STATE_POWER_UP, NODE_STATE_FLAGS, "POWER_UP"),
	add_flag_masked_bit(NODE_STATE_POWER_DRAIN, NODE_STATE_FLAGS, "POWER_DRAIN"),
	add_flag_masked_bit(NODE_STATE_DYNAMIC_NORM, NODE_STATE_FLAGS, "DYNAMIC_NORM"),
};

#define add_parse(mtype, field, path) \
	add_parser(node_info_t, mtype, false, field, 0, path, NEED_NONE)
#define add_cparse(mtype, path) \
	add_complex_parser(node_info_t, mtype, false, path, NEED_NONE)
static const parser_t PARSER_ARRAY(NODE)[] = {
	add_parse(STRING, arch, "architecture"),
	add_parse(STRING, bcast_address, "burstbuffer_network_address"),
	add_parse(UINT16, boards, "boards"),
	add_parse(UINT64, boot_time, "boot_time"),
	add_parse(STRING, cluster_name, "cluster_name"),
	add_parse(UINT16, cores, "cores"),
	add_parse(UINT16, core_spec_cnt, "specialized_cores"),
	add_parse(UINT32, cpu_bind, "cpu_binding"),
	add_parse(UINT32, cpu_load, "cpu_load"),
	add_parse(UINT64, free_mem, "free_mem"),
	add_parse(UINT16, cpus, "cpus"),
	add_parse(UINT16, cpus_efctv, "effective_cpus"),
	add_parse(STRING, cpu_spec_list, "specialized_cpus"),
	add_parse(ACCT_GATHER_ENERGY_PTR, energy, "energy"),
	add_parse(EXT_SENSORS_DATA_PTR, ext_sensors, "external_sensors"),
	add_parse(STRING, extra, "extra"),
	add_parse(POWER_MGMT_DATA_PTR, power, "power"),
	add_parse(STRING, features, "features"),
	add_parse(STRING, features_act, "active_features"),
	add_parse(STRING, gres, "gres"),
	add_parse(STRING, gres_drain, "gres_drained"),
	add_parse(STRING, gres_used, "gres_used"),
	add_parse(UINT64, last_busy, "last_busy"),
	add_parse(STRING, mcs_label, "mcs_label"),
	add_parse(UINT64, mem_spec_limit, "specialized_memory"),
	add_parse(STRING, name, "name"),
	add_parse(NODE_STATES_NO_VAL, next_state, "next_state_after_reboot"),
	add_parse(STRING, node_addr, "address"),
	add_parse(STRING, node_hostname, "hostname"),
	add_parse_bit_flag_array(node_info_t, NODE_STATES, false, node_state, "state"),
	add_parse(STRING, os, "operating_system"),
	add_parse(USER_ID, owner, "owner"),
	add_parse(CSV_LIST, partitions, "partitions"),
	add_parse(UINT16, port, "port"),
	add_parse(UINT64, real_memory, "real_memory"),
	add_parse(STRING, comment, "comment"),
	add_parse(STRING, reason, "reason"),
	add_parse(UINT64, reason_time, "reason_changed_at"),
	add_parse(USER_ID, reason_uid, "reason_set_by_user"),
	add_parse(UINT64, resume_after, "resume_after"),
	add_parse(STRING, resv_name, "reservation"),
	add_cparse(NODE_SELECT_ALLOC_MEMORY, "alloc_memory"),
	add_cparse(NODE_SELECT_ALLOC_CPUS, "alloc_cpus"),
	add_cparse(NODE_SELECT_ALLOC_IDLE_CPUS, "alloc_idle_cpus"),
	add_cparse(NODE_SELECT_TRES_USED, "tres_used"),
	add_cparse(NODE_SELECT_TRES_WEIGHTED, "tres_weighted"),
	add_parse(UINT64, slurmd_start_time, "slurmd_start_time"),
	add_parse(UINT16, sockets, "sockets"),
	add_parse(UINT16, threads, "threads"),
	add_parse(UINT32, tmp_disk, "temporary_disk"),
	add_parse(UINT32, weight, "weight"),
	add_parse(STRING, tres_fmt_str, "tres"),
	add_parse(STRING, version, "version"),
};
#undef add_parse
#undef add_cparse

#define add_parse(mtype, field, path) \
	add_parser(slurm_license_info_t, mtype, false, field, 0, path, NEED_NONE)
static const parser_t PARSER_ARRAY(LICENSE)[] = {
	add_parse(STRING, name, "LicenseName"),
	add_parse(UINT32, total, "Total"),
	add_parse(UINT32, in_use, "Used"),
	add_parse(UINT32, available, "Free"),
	add_parse_bool(slurm_license_info_t, LICENSE_FLAG_REMOTE, false, remote, "flags", "REMOTE", NEED_NONE),
	add_parse(UINT32, reserved, "Reserved"),
};
#undef add_parse

static const flag_bit_t PARSER_FLAG_ARRAY(JOB_FLAGS)[] = {
	add_flag_bit(KILL_INV_DEP, "KILL_INVALID_DEPENDENCY"),
	add_flag_bit(NO_KILL_INV_DEP, "NO_KILL_INVALID_DEPENDENCY"),
	add_flag_bit(HAS_STATE_DIR, "HAS_STATE_DIRECTORY"),
	add_flag_bit(BACKFILL_TEST, "TESTING_BACKFILL"),
	add_flag_bit(GRES_ENFORCE_BIND, "GRES_BINDING_ENFORCED"),
	add_flag_bit(TEST_NOW_ONLY, "TEST_NOW_ONLY"),
	add_flag_bit(JOB_SEND_ENV, "SEND_JOB_ENVIRONMENT"),
	add_flag_bit(SPREAD_JOB, "SPREAD_JOB"),
	add_flag_bit(USE_MIN_NODES, "PREFER_MINIMUM_NODE_COUNT"),
	add_flag_bit(JOB_KILL_HURRY, "JOB_KILL_HURRY"),
	add_flag_bit(TRES_STR_CALC, "SKIP_TRES_STRING_ACCOUNTING"),
	add_flag_bit(SIB_JOB_FLUSH, "SIBLING_CLUSTER_UPDATE_ONLY"),
	add_flag_bit(HET_JOB_FLAG, "HETEROGENEOUS_JOB"),
	add_flag_bit(JOB_NTASKS_SET, "EXACT_TASK_COUNT_REQUESTED"),
	add_flag_bit(JOB_CPUS_SET, "EXACT_CPU_COUNT_REQUESTED"),
	add_flag_bit(BF_WHOLE_NODE_TEST, "TESTING_WHOLE_NODE_BACKFILL"),
	add_flag_bit(TOP_PRIO_TMP, "TOP_PRIORITY_JOB"),
	add_flag_bit(JOB_ACCRUE_OVER, "ACCRUE_COUNT_CLEARED"),
	add_flag_bit(GRES_DISABLE_BIND, "GRED_BINDING_DISABLED"),
	add_flag_bit(JOB_WAS_RUNNING, "JOB_WAS_RUNNING"),
	add_flag_bit(RESET_ACCRUE_TIME, "JOB_ACCRUE_TIME_RESET"),
	add_flag_bit(CRON_JOB, "CRON_JOB"),
	add_flag_bit(JOB_MEM_SET, "EXACT_MEMORY_REQUESTED"),
	add_flag_bit(JOB_RESIZED, "JOB_RESIZED"),
	add_flag_bit(USE_DEFAULT_ACCT, "USING_DEFAULT_ACCOUNT"),
	add_flag_bit(USE_DEFAULT_PART, "USING_DEFAULT_PARTITION"),
	add_flag_bit(USE_DEFAULT_QOS, "USING_DEFAULT_QOS"),
	add_flag_bit(USE_DEFAULT_WCKEY, "USING_DEFAULT_WCKEY"),
	add_flag_bit(JOB_DEPENDENT, "DEPENDENT"),
	add_flag_bit(JOB_MAGNETIC, "MAGNETIC"),
	add_flag_bit(JOB_PART_ASSIGNED, "PARTITION_ASSIGNED"),
	add_flag_bit(BACKFILL_SCHED, "BACKFILL_ATTEMPTED"),
	add_flag_bit(BACKFILL_LAST, "SCHEDULING_ATTEMPTED"),
	add_flag_bit(JOB_SEND_SCRIPT, "SAVE_BATCH_SCRIPT"),
};

static const flag_bit_t PARSER_FLAG_ARRAY(JOB_SHOW_FLAGS)[] = {
	add_flag_bit(SHOW_ALL, "ALL"),
	add_flag_bit(SHOW_DETAIL, "DETAIL"),
	add_flag_bit(SHOW_MIXED, "MIXED"),
	add_flag_bit(SHOW_LOCAL, "LOCAL"),
	add_flag_bit(SHOW_SIBLING, "SIBLING"),
	add_flag_bit(SHOW_FEDERATION, "FEDERATION"),
	add_flag_bit(SHOW_FUTURE, "FUTURE"),
};

static const flag_bit_t PARSER_FLAG_ARRAY(POWER_FLAGS)[] = {
	add_flag_bit(SLURM_POWER_FLAGS_LEVEL, "EQUAL_POWER"),
};

static const flag_bit_t PARSER_FLAG_ARRAY(JOB_MAIL_FLAGS)[] = {
	add_flag_bit(MAIL_JOB_BEGIN, "BEGIN"),
	add_flag_bit(MAIL_JOB_END, "END"),
	add_flag_bit(MAIL_JOB_FAIL, "FAIL"),
	add_flag_bit(MAIL_JOB_REQUEUE, "REQUEUE"),
	add_flag_bit(MAIL_JOB_TIME100, "TIME=100%"),
	add_flag_bit(MAIL_JOB_TIME90, "TIME=90%"),
	add_flag_bit(MAIL_JOB_TIME80, "TIME=80%"),
	add_flag_bit(MAIL_JOB_TIME50, "TIME=50%"),
	add_flag_bit(MAIL_JOB_STAGE_OUT, "STAGE_OUT"),
	add_flag_bit(MAIL_ARRAY_TASKS, "ARRAY_TASKS"),
	add_flag_bit(MAIL_INVALID_DEPEND, "INVALID_DEPENDENCY"),
};

#define add_skip(field) \
	add_parser_skip(slurm_job_info_t, field)
#define add_parse(mtype, field, path) \
	add_parser(slurm_job_info_t, mtype, false, field, 0, path, NEED_NONE)
#define add_parse_overload(mtype, field, overloads, path) \
	add_parser(slurm_job_info_t, mtype, false, field, overloads, path, NEED_NONE)
#define add_cparse(mtype, path) \
	add_complex_parser(slurm_job_info_t, mtype, false, path, NEED_NONE)
static const parser_t PARSER_ARRAY(JOB_INFO)[] = {
	add_parse(STRING, account, "account"),
	add_parse(UINT64, accrue_time, "accrue_time"),
	add_parse(STRING, admin_comment, "admin_comment"),
	add_parse(STRING, alloc_node, "allocating_node"),
	add_skip(alloc_sid),
	add_skip(array_bitmap),
	add_parse(UINT32_NO_VAL, array_job_id, "array_job_id"),
	add_parse(UINT32_NO_VAL, array_task_id, "array_task_id"),
	add_parse(UINT32_NO_VAL, array_max_tasks, "array_max_tasks"),
	add_parse(STRING, array_task_str, "array_task_string"),
	add_parse(UINT32, assoc_id, "association_id"),
	add_parse(STRING, batch_features, "batch_features"),
	add_parse(BOOL16, batch_flag, "batch_flag"),
	add_parse(STRING, batch_host, "batch_host"),
	add_parse_bit_flag_array(slurm_job_info_t, JOB_FLAGS, false, bitflags, "flags"),
	add_skip(boards_per_node),
	add_parse(STRING, burst_buffer, "burst_buffer"),
	add_parse(STRING, burst_buffer_state, "burst_buffer_state"),
	add_parse(STRING, cluster, "cluster"),
	add_parse(STRING, cluster_features, "cluster_features"),
	add_parse(STRING, command, "command"),
	add_parse(STRING, comment, "comment"),
	add_parse(STRING, container, "container"),
	add_parse(STRING, container_id, "container_id"),
	add_parse(BOOL16_NO_VAL, contiguous, "contiguous"),
	add_parse_overload(CORE_SPEC, core_spec, 1, "core_spec"),
	add_parse_overload(THREAD_SPEC, core_spec, 1, "thread_spec"),
	add_parse(UINT16_NO_VAL, cores_per_socket, "cores_per_socket"),
	add_parse(FLOAT64_NO_VAL, billable_tres, "billable_tres"),
	add_parse(UINT16_NO_VAL, cpus_per_task, "cpus_per_task"),
	add_parse(UINT32_NO_VAL, cpu_freq_min, "cpu_frequency_minimum"),
	add_parse(UINT32_NO_VAL, cpu_freq_max, "cpu_frequency_maximum"),
	add_parse(UINT32_NO_VAL, cpu_freq_gov, "cpu_frequency_governor"),
	add_parse(STRING, cpus_per_tres, "cpus_per_tres"),
	add_parse(STRING, cronspec, "cron"),
	add_parse(UINT64, deadline, "deadline"),
	add_parse(UINT32_NO_VAL, delay_boot, "delay_boot"),
	add_parse(STRING, dependency, "dependency"),
	add_parse(UINT32, derived_ec, "derived_exit_code"),
	add_parse(UINT64, eligible_time, "eligible_time"),
	add_parse(UINT64, end_time, "end_time"),
	add_parse(STRING, exc_nodes, "excluded_nodes"),
	add_skip(exc_node_inx),
	add_parse(UINT32, exit_code, "exit_code"),
	add_parse(STRING, extra, "extra"),
	add_parse(STRING, failed_node, "failed_node"),
	add_parse(STRING, features, "features"),
	add_parse(STRING, fed_origin_str, "federation_origin"),
	add_skip(fed_siblings_active),
	add_parse(STRING, fed_siblings_active_str, "federation_siblings_active"),
	add_skip(fed_siblings_viable),
	add_parse(STRING, fed_siblings_viable_str, "federation_siblings_viable"),
	add_skip(gres_detail_cnt),
	add_skip(gres_detail_str), /* handled by JOB_INFO_GRES_DETAIL */
	add_cparse(JOB_INFO_GRES_DETAIL, "gres_detail"),
	add_parse_overload(UINT32, group_id, 1, "group_id"),
	add_parse_overload(GROUP_ID, group_id, 1, "group_name"),
	add_parse(UINT32_NO_VAL, het_job_id, "het_job_id"),
	add_parse(STRING, het_job_id_set, "het_job_id_set"),
	add_parse(UINT32_NO_VAL, het_job_offset, "het_job_offset"),
	add_parse(UINT32_NO_VAL, job_id, "job_id"),
	add_parse(JOB_RES_PTR, job_resrcs, "job_resources"),
	add_parse(JOB_STATE, job_state, "job_state"),
	add_parse(UINT64, last_sched_eval, "last_sched_evaluation"),
	add_parse(STRING, licenses, "licenses"),
	add_parse_bit_flag_array(slurm_job_info_t, JOB_MAIL_FLAGS, false, mail_type, "mail_type"),
	add_parse(STRING, mail_user, "mail_user"),
	add_parse(UINT32_NO_VAL, max_cpus, "max_cpus"),
	add_parse(UINT32_NO_VAL, max_nodes, "max_nodes"),
	add_parse(STRING, mcs_label, "mcs_label"),
	add_parse(STRING, mem_per_tres, "memory_per_tres"),
	add_parse(STRING, name, "name"),
	add_parse(STRING, network, "network"),
	add_parse(STRING, nodes, "nodes"),
	add_parse(NICE, nice, "nice"),
	add_parse(UINT16_NO_VAL, ntasks_per_core, "tasks_per_core"),
	add_parse(UINT16_NO_VAL, ntasks_per_tres, "tasks_per_tres"),
	add_parse(UINT16_NO_VAL, ntasks_per_node, "tasks_per_node"),
	add_parse(UINT16_NO_VAL, ntasks_per_socket, "tasks_per_socket"),
	add_parse(UINT16_NO_VAL, ntasks_per_board, "tasks_per_board"),
	add_parse(UINT32_NO_VAL, num_cpus, "cpus"),
	add_parse(UINT32_NO_VAL, num_nodes, "node_count"),
	add_parse(UINT32_NO_VAL, num_tasks, "tasks"),
	add_parse(STRING, partition, "partition"),
	add_parse(STRING, prefer, "prefer"),
	add_parse_overload(JOB_MEM_PER_CPU, pn_min_memory, 1, "memory_per_cpu"),
	add_parse_overload(JOB_MEM_PER_NODE, pn_min_memory, 1, "memory_per_node"),
	add_parse(UINT16_NO_VAL, pn_min_cpus, "minimum_cpus_per_node"),
	add_parse(UINT32_NO_VAL, pn_min_tmp_disk, "minimum_tmp_disk_per_node"),
	add_parse_bit_flag_array(slurm_job_info_t, POWER_FLAGS, false, power_flags, "power/flags"),
	add_parse(UINT64, preempt_time, "preempt_time"),
	add_parse(UINT64, preemptable_time, "preemptable_time"),
	add_parse(UINT64, pre_sus_time, "pre_sus_time"),
	add_parse(UINT32_NO_VAL, priority, "priority"),
	add_parse(ACCT_GATHER_PROFILE, profile, "profile"),
	add_parse(QOS_NAME, qos, "qos"),
	add_parse(BOOL, reboot, "reboot"),
	add_parse(STRING, req_nodes, "required_nodes"),
	add_skip(req_node_inx),
	add_parse(UINT32, req_switch, "minimum_switches"),
	add_parse(BOOL16, requeue, "requeue"),
	add_parse(UINT64, resize_time, "resize_time"),
	add_parse(UINT16, restart_cnt, "restart_cnt"),
	add_parse(STRING, resv_name, "resv_name"),
	add_parse(STRING, sched_nodes, "scheduled_nodes"),
	add_skip(select_jobinfo),
	add_parse(STRING, selinux_context, "selinux_context"),
	add_parse(JOB_SHARED, shared, "shared"),
	add_parse_bit_flag_array(slurm_job_info_t, JOB_SHOW_FLAGS, false, show_flags, "show_flags"),
	add_parse(UINT16, sockets_per_board, "sockets_per_board"),
	add_parse(UINT16_NO_VAL, sockets_per_node, "sockets_per_node"),
	add_parse(UINT64, start_time, "start_time"),
	add_skip(start_protocol_ver),
	add_parse(STRING, state_desc, "state_description"),
	add_parse(JOB_REASON, state_reason, "state_reason"),
	add_skip(std_err),
	add_skip(std_in),
	add_skip(std_out),
	add_cparse(JOB_INFO_STDERR, "standard_error"),
	add_cparse(JOB_INFO_STDIN, "standard_input"),
	add_cparse(JOB_INFO_STDOUT, "standard_output"),
	add_parse(UINT64, submit_time, "submit_time"),
	add_parse(UINT64, suspend_time, "suspend_time"),
	add_parse(STRING, system_comment, "system_comment"),
	add_parse(UINT32_NO_VAL, time_limit, "time_limit"),
	add_parse(UINT32_NO_VAL, time_min, "time_minimum"),
	add_parse(UINT16_NO_VAL, threads_per_core, "threads_per_core"),
	add_parse(STRING, tres_bind, "tres_bind"),
	add_parse(STRING, tres_freq, "tres_freq"),
	add_parse(STRING, tres_per_job, "tres_per_job"),
	add_parse(STRING, tres_per_node, "tres_per_node"),
	add_parse(STRING, tres_per_socket, "tres_per_socket"),
	add_parse(STRING, tres_per_task, "tres_per_task"),
	add_parse(STRING, tres_req_str, "tres_req_str"),
	add_parse(STRING, tres_alloc_str, "tres_alloc_str"),
	add_parse_overload(UINT32, user_id, 1, "user_id"),
	add_parse_overload(USER_ID, user_id, 1, "user_name"),
	add_parse(UINT32, wait4switch, "maximum_switch_wait_time"),
	add_parse(STRING, wckey, "wckey"),
	add_parse(STRING, work_dir, "current_working_directory"),
};
#undef add_parse
#undef add_parse_overload
#undef add_cparse
#undef add_skip

#define add_parse(mtype, field, path) \
	add_parser(job_resources_t, mtype, false, field, 0, path, NEED_NONE)
#define add_parse_overload(mtype, field, overloads, path) \
	add_parser(job_resources_t, mtype, false, field, overloads, path, NEED_NONE)
#define add_cparse(mtype, path) \
	add_complex_parser(job_resources_t, mtype, false, path, NEED_NONE)
static const parser_t PARSER_ARRAY(JOB_RES)[] = {
	add_parse(STRING, nodes, "nodes"),
	add_parse_overload(ALLOCATED_CORES, ncpus, 1, "allocated_cores"),
	add_parse_overload(ALLOCATED_CPUS, ncpus, 1, "allocated_cpus"),
	add_parse(UINT32, nhosts, "allocated_hosts"),
	add_cparse(JOB_RES_NODES, "allocated_nodes"),
};
#undef add_parse
#undef add_parse_overload
#undef add_cparse

#define add_parse(mtype, field, path) \
	add_parser(controller_ping_t, mtype, false, field, 0, path, NEED_NONE)
static const parser_t PARSER_ARRAY(CONTROLLER_PING)[] = {
	add_parse(STRING, hostname, "hostname"),
	add_parse(CONTROLLER_PING_RESULT, pinged, "pinged"),
	add_parse(UINT64, latency, "latency"),
	add_parse(CONTROLLER_PING_MODE, offset, "mode"),
};
#undef add_parse

#define add_parse(mtype, field, path) \
	add_parser(job_step_info_t, mtype, false, field, 0, path, NEED_NONE)
#define add_skip(field) \
	add_parser_skip(job_step_info_t, field)
static const parser_t PARSER_ARRAY(STEP_INFO)[] = {
	add_parse(UINT32, array_job_id, "array/job_id"),
	add_parse(UINT32, array_task_id, "array/task_id"),
	add_parse(STRING, cluster, "cluster"),
	add_parse(STRING, container, "container"),
	add_parse(STRING, container_id, "container_id"),
	add_parse(UINT32_NO_VAL, cpu_freq_min, "cpu/frequency/min"),
	add_parse(UINT32_NO_VAL, cpu_freq_max, "cpu/frequency/max"),
	add_parse(UINT32_NO_VAL, cpu_freq_gov, "cpu/frequency/governor"),
	add_parse(STRING, cpus_per_tres, "tres/per/cpu"),
	add_parse(STRING, mem_per_tres, "tres/per/memory"),
	add_parse(STRING, name, "name"),
	add_parse(STRING, network, "network"),
	add_parse(STRING, nodes, "nodes"),
	add_skip(node_inx),
	add_parse(UINT32, num_cpus, "number_cpus"),
	add_parse(UINT32, num_tasks, "number_tasks"),
	add_parse(STRING, partition, "partition"),
	add_parse(STRING, resv_ports, "reserved_ports"),
	add_parse(UINT64, run_time, "time/running"),
	add_skip(select_jobinfo),
	add_parse(STRING, srun_host, "srun/host"),
	add_parse(UINT32, srun_pid, "srun/pid"),
	add_parse(UINT64, start_time, "time/start"),
	add_skip(start_protocol_ver),
	add_parse(JOB_STATE, state, "state"),
	add_parse(SLURM_STEP_ID, step_id, "id"),
	add_parse(STRING, submit_line, "submit_line"),
	add_parse(TASK_DISTRIBUTION, task_dist, "task/distribution"),
	add_parse(UINT32_NO_VAL, time_limit, "time/limit"),
	add_parse(STRING, tres_alloc_str, "tres/allocation"),
	add_parse(STRING, tres_bind, "tres/binding"),
	add_parse(STRING, tres_freq, "tres/frequency"),
	add_parse(STRING, tres_per_step, "tres/per/step"),
	add_parse(STRING, tres_per_node, "tres/per/node"),
	add_parse(STRING, tres_per_socket, "tres/per/socket"),
	add_parse(STRING, tres_per_task, "tres/per/task"),
	add_parse(USER_ID, user_id, "user"),
};
#undef add_parse
#undef add_skip

#define add_parse(mtype, field, path) \
	add_parser(partition_info_t, mtype, false, field, 0, path, NEED_NONE)
#define add_skip(field) \
	add_parser_skip(partition_info_t, field)
static const parser_t PARSER_ARRAY(PARTITION_INFO)[] = {
	add_parse(STRING, allow_alloc_nodes, "nodes/allowed_allocation"),
	add_parse(STRING, allow_accounts, "accounts/allowed"),
	add_parse(STRING, allow_groups, "groups/allowed"),
	add_parse(STRING, allow_qos, "qos/allowed"),
	add_parse(STRING, alternate, "alternate"),
	add_parse(STRING, billing_weights_str, "tres/billing_weights"),
	add_parse(STRING, cluster_name, "cluster"),
	add_skip(cr_type), //FIXME:wtf is this
	add_parse(UINT32, cpu_bind, "cpus/task_binding"),
	add_parse(UINT64, def_mem_per_cpu, "defaults/memory_per_cpu"),
	add_parse(UINT32_NO_VAL, default_time, "defaults/time"),
	add_parse(STRING, deny_accounts, "accounts/deny"),
	add_parse(STRING, deny_qos, "qos/deny"),
	add_skip(flags), //FIXME
	add_parse(UINT32, grace_time, "grace_time"),
	add_skip(job_defaults_list), //FIXME - is this even packed?
	add_parse(STRING, job_defaults_str, "defaults/job"),
	add_parse(UINT32_NO_VAL, max_cpus_per_node, "maximums/cpus_per_node"),
	add_parse(UINT32_NO_VAL, max_cpus_per_socket, "maximums/cpus_per_socket"),
	add_parse(UINT64, max_mem_per_cpu, "maximums/memory_per_cpu"),
	add_parse(UINT32_NO_VAL, max_nodes, "maximums/nodes"),
	add_parse(UINT16, max_share, "maximums/shares"),
	add_parse(UINT32_NO_VAL, max_time, "maximums/time"),
	add_parse(UINT32, min_nodes, "minimums/nodes"),
	add_parse(STRING, name, "name"),
	add_skip(node_inx),
	add_parse(STRING, nodes, "nodes/configured"),
	add_parse(STRING, nodesets, "node_sets"),
	add_parse(UINT16_NO_VAL, over_time_limit, "maximums/over_time_limit"),
	add_skip(preempt_mode), // FIXME
	add_parse(UINT16, priority_job_factor, "priority/job_factor"),
	add_parse(UINT16, priority_tier, "priority/tier"),
	add_parse(STRING, qos_char, "qos/assigned"),
	add_parse(UINT16_NO_VAL, resume_timeout, "timeouts/resume"),
	add_skip(state_up), //FIXME
	add_parse(UINT32_NO_VAL, suspend_time, "suspend_time"),
	add_parse(UINT16_NO_VAL, suspend_timeout, "timeouts/suspend"),
	add_parse(UINT32, total_cpus, "cpus/total"),
	add_parse(UINT32, total_nodes, "nodes/total"),
	add_parse(STRING, tres_fmt_str, "tres/configured"),
};
#undef add_parse
#undef add_skip

#define add_parse(mtype, field, path) \
	add_parser(sinfo_data_t, mtype, false, field, 0, path, NEED_NONE)
#define add_skip(field) \
	add_parser_skip(sinfo_data_t, field)
static const parser_t PARSER_ARRAY(SINFO_DATA)[] = {
	add_parse(UINT16, port, "port"),
	add_parse_bit_flag_array(sinfo_data_t, NODE_STATES, false, node_state, "node/state"),
	add_parse(UINT32, nodes_alloc, "nodes/allocated"),
	add_parse(UINT32, nodes_idle, "nodes/idle"),
	add_parse(UINT32, nodes_other, "nodes/other"),
	add_parse(UINT32, nodes_total, "nodes/total"),
	add_parse(UINT32, cpus_alloc, "cpus/allocated"),
	add_parse(UINT32, cpus_idle, "cpus/idle"),
	add_parse(UINT32, cpus_other, "cpus/other"),
	add_parse(UINT32, cpus_total, "cpus/total"),
	add_parse(UINT32, min_cpus, "cpus/mininum"),
	add_parse(UINT32, max_cpus, "cpus/maximum"),
	add_parse(UINT32, min_sockets, "sockets/mininum"),
	add_parse(UINT32, max_sockets, "sockets/maximum"),
	add_parse(UINT32, min_cores, "cores/mininum"),
	add_parse(UINT32, max_cores, "cores/maximum"),
	add_parse(UINT32, min_threads, "threads/mininum"),
	add_parse(UINT32, max_threads, "threads/maximum"),
	add_parse(UINT32, min_disk, "disk/mininum"),
	add_parse(UINT32, max_disk, "disk/maximum"),
	add_parse(UINT64, min_mem, "memory/mininum"),
	add_parse(UINT64, max_mem, "memory/maximum"),
	add_parse(UINT32, min_weight, "weight/mininum"),
	add_parse(UINT32, max_weight, "weight/maximum"),
	add_parse(UINT32, min_cpu_load, "cpus/load/mininum"),
	add_parse(UINT32, max_cpu_load, "cpus/load/maximum"),
	add_parse(UINT64, min_free_mem, "memory/free/mininum"),
	add_parse(UINT64, max_free_mem, "memory/free/maximum"),
	add_parse(UINT32_NO_VAL, max_cpus_per_node, "cpus/per_node/max"),
	add_parse(UINT64, alloc_memory, "memory/allocated"),
	add_parse(STRING, features, "features/total"),
	add_parse(STRING, features_act, "features/active"),
	add_parse(STRING, gres, "gres/total"),
	add_parse(STRING, gres_used, "gres/used"),
	add_parse(STRING, cluster_name, "cluster"),
	add_parse(STRING, comment, "comment"),
	add_parse(STRING, extra, "extra"),
	add_parse(STRING, reason, "reason/description"),
	add_parse(UINT64, reason_time, "reason/time"),
	add_parse(STRING, resv_name, "reservation"),
	add_parse(USER_ID, reason_uid, "reason/user"),
	add_skip(version), /* already in meta */
	add_parse(HOSTLIST, hostnames, "nodes/hostnames"),
	add_parse(HOSTLIST, node_addr, "nodes/addresses"),
	add_parse(HOSTLIST, nodes, "nodes/nodes"),
	add_parse(PARTITION_INFO_PTR, part_info, "partition"),
	add_skip(part_inx),
};
#undef add_parse
#undef add_skip

#define add_parse(mtype, field, path) \
	add_parser(acct_gather_energy_t, mtype, false, field, 0, path, NEED_NONE)
static const parser_t PARSER_ARRAY(ACCT_GATHER_ENERGY)[] = {
	add_parse(UINT32, ave_watts, "average_watts"),
	add_parse(UINT64, base_consumed_energy, "base_consumed_energy"),
	add_parse(UINT64, consumed_energy, "consumed_energy"),
	add_parse(UINT32, current_watts, "current_watts"),
	add_parse(UINT64, previous_consumed_energy, "previous_consumed_energy"),
	add_parse(UINT64, poll_time, "last_collected"),
};
#undef add_parse

#define add_parse(mtype, field, path) \
	add_parser(ext_sensors_data_t, mtype, false, field, 0, path, NEED_NONE)
static const parser_t PARSER_ARRAY(EXT_SENSORS_DATA)[] = {
	add_parse(UINT64, consumed_energy, "consumed_energy"),
	add_parse(UINT32, temperature, "temperature"),
	add_parse(UINT64, energy_update_time, "energy_update_time"),
	add_parse(UINT32, current_watts, "current_watts"),
};
#undef add_parse

#define add_parse(mtype, field, path) \
	add_parser(power_mgmt_data_t, mtype, false, field, 0, path, NEED_NONE)
static const parser_t PARSER_ARRAY(POWER_MGMT_DATA)[] = {
	add_parse(UINT32, cap_watts, "maximum_watts"),
	add_parse(UINT32, current_watts, "current_watts"),
	add_parse(UINT64, joule_counter, "total_energy"),
	add_parse(UINT32, new_cap_watts, "new_maximum_watts"),
	add_parse(UINT32, max_watts, "peak_watts"),
	add_parse(UINT32, min_watts, "lowest_watts"),
	add_parse(UINT64, new_job_time, "new_job_time"),
	add_parse(UINT16, state, "state"),
	add_parse(UINT64, time_usec, "time_start_day"),
};
#undef add_parse

static const flag_bit_t PARSER_FLAG_ARRAY(RESERVATION_FLAGS)[] = {
	add_flag_bit(RESERVE_FLAG_MAINT, "MAINT"),
	add_flag_bit(RESERVE_FLAG_NO_MAINT, "NO_MAINT"),
	add_flag_bit(RESERVE_FLAG_DAILY, "DAILY"),
	add_flag_bit(RESERVE_FLAG_NO_DAILY, "NO_DAILY"),
	add_flag_bit(RESERVE_FLAG_WEEKLY, "WEEKLY"),
	add_flag_bit(RESERVE_FLAG_NO_WEEKLY, "NO_WEEKLY"),
	add_flag_bit(RESERVE_FLAG_IGN_JOBS, "IGNORE_JOBS"),
	add_flag_bit(RESERVE_FLAG_NO_IGN_JOB, "NO_IGNORE_JOBS"),
	add_flag_bit(RESERVE_FLAG_ANY_NODES, "ANY_NODES"),
	add_flag_bit(RESERVE_FLAG_STATIC, "STATIC"),
	add_flag_bit(RESERVE_FLAG_NO_STATIC, "NO_STATIC"),
	add_flag_bit(RESERVE_FLAG_PART_NODES, "PART_NODES"),
	add_flag_bit(RESERVE_FLAG_NO_PART_NODES, "NO_PART_NODES"),
	add_flag_bit(RESERVE_FLAG_OVERLAP, "OVERLAP"),
	add_flag_bit(RESERVE_FLAG_SPEC_NODES, "SPEC_NODES"),
	add_flag_bit(RESERVE_FLAG_FIRST_CORES, "FIRST_CORES"),
	add_flag_bit(RESERVE_FLAG_TIME_FLOAT, "TIME_FLOAT"),
	add_flag_bit(RESERVE_FLAG_REPLACE, "REPLACE"),
	add_flag_bit(RESERVE_FLAG_ALL_NODES, "ALL_NODES"),
	add_flag_bit(RESERVE_FLAG_PURGE_COMP, "PURGE_COMP"),
	add_flag_bit(RESERVE_FLAG_WEEKDAY, "WEEKDAY"),
	add_flag_bit(RESERVE_FLAG_NO_WEEKDAY, "NO_WEEKDAY"),
	add_flag_bit(RESERVE_FLAG_WEEKEND, "WEEKEND"),
	add_flag_bit(RESERVE_FLAG_NO_WEEKEND, "NO_WEEKEND"),
	add_flag_bit(RESERVE_FLAG_FLEX, "FLEX"),
	add_flag_bit(RESERVE_FLAG_NO_FLEX, "NO_FLEX"),
	add_flag_bit(RESERVE_FLAG_DUR_PLUS, "DURATION_PLUS"),
	add_flag_bit(RESERVE_FLAG_DUR_MINUS, "DURATION_MINUS"),
	add_flag_bit(RESERVE_FLAG_NO_HOLD_JOBS, "NO_HOLD_JOBS_AFTER_END"),
	add_flag_bit(RESERVE_FLAG_NO_PURGE_COMP, "NO_PURGE_COMP"),
	add_flag_bit(RESERVE_FLAG_MAGNETIC, "MAGNETIC"),
	add_flag_bit(RESERVE_FLAG_SKIP, "SKIP"),
	add_flag_bit(RESERVE_FLAG_HOURLY, "HOURLY"),
	add_flag_bit(RESERVE_FLAG_NO_HOURLY, "NO_HOURLY"),
	add_flag_bit(RESERVE_REOCCURRING, "REOCCURRING"),
};

#define add_parse(mtype, field, path) \
	add_parser(resv_core_spec_t, mtype, false, field, 0, path, NEED_NONE)
static const parser_t PARSER_ARRAY(RESERVATION_CORE_SPEC)[] = {
	add_parse(STRING, node_name, "node"),
	add_parse(STRING, core_id, "core"),
};
#undef add_parse

#define add_cparse(mtype, path) \
	add_complex_parser(reserve_info_t, mtype, false, path, NEED_NONE)
#define add_parse(mtype, field, path) \
	add_parser(reserve_info_t, mtype, false, field, 0, path, NEED_NONE)
#define add_skip(field) \
	add_parser_skip(reserve_info_t, field)
static const parser_t PARSER_ARRAY(RESERVATION_INFO)[] = {
	add_parse(STRING, accounts, "accounts"),
	add_parse(STRING, burst_buffer, "burst_buffer"),
	add_parse(UINT32, core_cnt, "core_count"),
	add_skip(core_spec_cnt), /* parsed by INFO_CORE_SPEC */
	add_skip(core_spec), /* parsed by INFO_CORE_SPEC */
	add_cparse(RESERVATION_INFO_CORE_SPEC, "core_specializations"),
	add_parse(UINT64, end_time, "end_time"),
	add_parse(STRING, features, "features"),
	add_parse_bit_flag_array(reserve_info_t, RESERVATION_FLAGS, false, flags, "flags"),
	add_parse(STRING, groups, "groups"),
	add_parse(STRING, licenses, "licenses"),
	add_parse(UINT32, max_start_delay, "max_start_delay"),
	add_parse(STRING, name, "name"),
	add_parse(UINT32, node_cnt, "node_count"),
	add_skip(node_inx),
	add_parse(STRING, node_list, "node_list"),
	add_parse(STRING, partition, "partition"),
	add_parse(UINT32_NO_VAL, purge_comp_time, "purge_completed/time"),
	add_parse(UINT64, start_time, "start_time"),
	add_parse(UINT32, resv_watts, "watts"),
	add_parse(STRING, tres_str, "tres"),
	add_parse(STRING, users, "users"),
};
#undef add_parse
#undef add_cparse
#undef add_skip

#define add_parse(mtype, field, path) \
	add_parser(submit_response_msg_t, mtype, false, field, 0, path, NEED_NONE)
#define add_parse_overload(mtype, field, overloads, path) \
	add_parser(submit_response_msg_t, mtype, false, field, overloads, path, NEED_NONE)
static const parser_t PARSER_ARRAY(JOB_SUBMIT_RESPONSE_MSG)[] = {
	add_parse(UINT32, job_id, "job_id"),
	add_parse(STEP_ID, step_id, "step_id"),
	add_parse_overload(UINT32, error_code, 1, "error_code"),
	add_parse_overload(ERROR, error_code, 1, "error"),
	add_parse(STRING, job_submit_user_msg, "job_submit_user_msg"),
};
#undef add_parse_overload
#undef add_parse

/* flag values based on output of slurm_sprint_cpu_bind_type() */
static const flag_bit_t PARSER_FLAG_ARRAY(CPU_BINDING_FLAGS)[] = {
	add_flag_masked_bit(CPU_BIND_VERBOSE, CPU_BIND_VERBOSE, "VERBOSE"),
	add_flag_equal(CPU_BIND_TO_THREADS, CPU_BIND_T_TO_MASK, "CPU_BIND_TO_THREADS"),
	add_flag_equal(CPU_BIND_TO_CORES, CPU_BIND_T_TO_MASK, "CPU_BIND_TO_CORES"),
	add_flag_equal(CPU_BIND_TO_SOCKETS, CPU_BIND_T_TO_MASK, "CPU_BIND_TO_SOCKETS"),
	add_flag_equal(CPU_BIND_TO_LDOMS, CPU_BIND_T_TO_MASK, "CPU_BIND_TO_LDOMS"),
	add_flag_equal(CPU_BIND_NONE, CPU_BIND_T_MASK, "CPU_BIND_NONE"),
	add_flag_equal(CPU_BIND_RANK, CPU_BIND_T_MASK, "CPU_BIND_RANK"),
	add_flag_equal(CPU_BIND_MAP, CPU_BIND_T_MASK, "CPU_BIND_MAP"),
	add_flag_equal(CPU_BIND_MASK, CPU_BIND_T_MASK, "CPU_BIND_MASK"),
	add_flag_equal(CPU_BIND_LDRANK, CPU_BIND_T_MASK, "CPU_BIND_LDRANK"),
	add_flag_equal(CPU_BIND_LDMAP, CPU_BIND_T_MASK, "CPU_BIND_LDMAP"),
	add_flag_equal(CPU_BIND_LDMASK, CPU_BIND_T_MASK, "CPU_BIND_LDMASK"),
	add_flag_masked_bit(CPU_BIND_ONE_THREAD_PER_CORE, CPU_BIND_ONE_THREAD_PER_CORE, "CPU_BIND_ONE_THREAD_PER_CORE"),
	add_flag_equal(CPU_AUTO_BIND_TO_THREADS, CPU_BIND_T_AUTO_TO_MASK, "CPU_AUTO_BIND_TO_THREADS"),
	add_flag_equal(CPU_AUTO_BIND_TO_CORES, CPU_BIND_T_AUTO_TO_MASK, "CPU_AUTO_BIND_TO_CORES"),
	add_flag_equal(CPU_AUTO_BIND_TO_SOCKETS, CPU_BIND_T_AUTO_TO_MASK, "CPU_AUTO_BIND_TO_SOCKETS"),
	add_flag_masked_bit(SLURMD_OFF_SPEC, CPU_BIND_T_TASK_PARAMS_MASK, "SLURMD_OFF_SPEC"),
	add_flag_masked_bit(CPU_BIND_OFF, CPU_BIND_T_TASK_PARAMS_MASK, "CPU_BIND_OFF"),
};

static const flag_bit_t PARSER_FLAG_ARRAY(CRON_ENTRY_FLAGS)[] = {
	add_flag_bit(CRON_WILD_MINUTE, "WILD_MINUTE"),
	add_flag_bit(CRON_WILD_HOUR, "WILD_HOUR"),
	add_flag_bit(CRON_WILD_DOM, "WILD_DAY_OF_MONTH"),
	add_flag_bit(CRON_WILD_MONTH, "WILD_MONTH"),
	add_flag_bit(CRON_WILD_DOW, "WILD_DAY_OF_WEEK"),
};

#define add_parse(mtype, field, path) \
	add_parser(cron_entry_t, mtype, false, field, 0, path, NEED_NONE)
static const parser_t PARSER_ARRAY(CRON_ENTRY)[] = {
	add_parse_bit_flag_array(cron_entry_t, CRON_ENTRY_FLAGS, false, flags, "flags"),
	add_parse(BITSTR, minute, "minute"),
	add_parse(BITSTR, hour, "hour"),
	add_parse(BITSTR, day_of_month, "day_of_month"),
	add_parse(BITSTR, month, "month"),
	add_parse(BITSTR, day_of_week, "day_of_week"),
	add_parse(STRING, cronspec, "specification"),
	add_parse(STRING, command, "command"),
	add_parse(UINT32, line_start, "line/start"),
	add_parse(UINT32, line_end, "line/end"),
};
#undef add_parse

static const flag_bit_t PARSER_FLAG_ARRAY(MEMORY_BINDING_TYPE)[] = {
	add_flag_masked_bit(MEM_BIND_VERBOSE, MEM_BIND_VERBOSE, "VERBOSE"),
	add_flag_equal(MEM_BIND_NONE, MEM_BIND_TYPE_MASK, "NONE"),
	add_flag_equal(MEM_BIND_RANK, MEM_BIND_TYPE_MASK, "RANK"),
	add_flag_equal(MEM_BIND_MAP, MEM_BIND_TYPE_MASK, "MAP"),
	add_flag_equal(MEM_BIND_MASK, MEM_BIND_TYPE_MASK, "MASK"),
	add_flag_equal(MEM_BIND_LOCAL, MEM_BIND_TYPE_MASK, "LOCAL"),
	add_flag_masked_bit(MEM_BIND_SORT, MEM_BIND_TYPE_FLAGS_MASK, "SORT"),
	add_flag_masked_bit(MEM_BIND_PREFER, MEM_BIND_TYPE_FLAGS_MASK, "PREFER"),
};

static const flag_bit_t PARSER_FLAG_ARRAY(OPEN_MODE)[] = {
	add_flag_bit(OPEN_MODE_APPEND, "APPEND"),
	add_flag_bit(OPEN_MODE_TRUNCATE, "TRUNCATE"),
};

static const flag_bit_t PARSER_FLAG_ARRAY(WARN_FLAGS)[] = {
	add_flag_bit(KILL_JOB_BATCH, "BATCH_JOB"),
	add_flag_bit(KILL_JOB_ARRAY, "ARRAY_JOB"),
	add_flag_bit(KILL_STEPS_ONLY, "FULL_STEPS_ONLY"),
	add_flag_bit(KILL_FULL_JOB, "FULL_JOB"),
	add_flag_bit(KILL_FED_REQUEUE, "FEDERATION_REQUEUE"),
	add_flag_bit(KILL_HURRY, "HURRY"),
	add_flag_bit(KILL_OOM, "OUT_OF_MEMORY"),
	add_flag_bit(KILL_NO_SIBS, "NO_SIBLING_JOBS"),
	add_flag_bit(KILL_JOB_RESV, "RESERVATION_JOB"),
	add_flag_bit(WARN_SENT, "WARNING_SENT"),
};

static const flag_bit_t PARSER_FLAG_ARRAY(X11_FLAGS)[] = {
	add_flag_bit(X11_FORWARD_ALL, "FORWARD_ALL_NODES"),
	add_flag_bit(X11_FORWARD_BATCH, "BATCH_NODE"),
	add_flag_bit(X11_FORWARD_FIRST, "FIRST_NODE"),
	add_flag_bit(X11_FORWARD_LAST, "LAST_NODE"),
};

#define add_cparse(mtype, path) \
	add_complex_parser(job_desc_msg_t, mtype, false, path, NEED_NONE)
#define add_parse(mtype, field, path) \
	add_parser(job_desc_msg_t, mtype, false, field, 0, path, NEED_NONE)
#define add_parse_overload(mtype, field, overloads, path) \
	add_parser(job_desc_msg_t, mtype, false, field, overloads, path, NEED_NONE)
#define add_skip(field) \
	add_parser_skip(job_desc_msg_t, field)
#define add_flags(mtype, field, path) \
	add_parse_bit_flag_array(job_desc_msg_t, mtype, false, field, path)
static const parser_t PARSER_ARRAY(JOB_DESC_MSG)[] = {
	add_parse(STRING, account, "account"),
	add_parse(STRING, acctg_freq, "account_gather_frequency"),
	add_parse(STRING, admin_comment, "admin_comment"),
	add_parse(STRING, alloc_node, "allocation_node_list"),
	add_parse(UINT16, alloc_resp_port, "allocation_node_port"),
	add_skip(alloc_sid),
	add_skip(argc),
	add_skip(argv),
	add_cparse(JOB_DESC_MSG_ARGV, "argv"),
	add_parse(STRING, array_inx, "arraytaskthrottle"),
	add_skip(array_bitmap),
	add_parse(STRING, batch_features, "batch_features"),
	add_parse(UINT64, begin_time, "begin_time"),
	add_flags(JOB_FLAGS, bitflags, "flags"),
	add_parse(STRING, burst_buffer, "burst_buffer"),
	add_parse(STRING, clusters, "clusters"),
	add_parse(STRING, cluster_features, "cluster_constraint"),
	add_parse(STRING, comment, "comment"),
	add_parse(BOOL16, contiguous, "contiguous"),
	add_parse(STRING, container, "container"),
	add_parse(STRING, container_id, "container_id"),
	add_parse_overload(CORE_SPEC, core_spec, 1, "core_specification"),
	add_parse_overload(THREAD_SPEC, core_spec, 1, "thread_specification"),
	add_parse(STRING, cpu_bind, "cpu_binding"),
	add_flags(CPU_BINDING_FLAGS, cpu_bind_type, "cpu_binding_flags"),
	add_cparse(JOB_DESC_MSG_CPU_FREQ, "cpu_frequency"),
	add_skip(cpu_freq_min),
	add_skip(cpu_freq_max),
	add_skip(cpu_freq_gov),
	add_parse(STRING, cpus_per_tres, "cpus_per_tres"),
	add_parse(CRON_ENTRY_PTR, crontab_entry, "crontab"),
	add_parse(UINT64, deadline, "deadline"),
	add_parse(UINT32, delay_boot, "delay_boot"),
	add_parse(STRING, dependency, "dependency"),
	add_parse(UINT64, end_time, "end_time"),
	add_cparse(JOB_DESC_MSG_ENV, "environment"),
	add_skip(environment),
	add_skip(env_hash),
	add_skip(env_size),
	add_parse(CSV_LIST, exc_nodes, "excluded_nodes"),
	add_parse(STRING, extra, "extra"),
	add_parse(STRING, features, "constraints"),
	add_skip(fed_siblings_active),
	add_skip(fed_siblings_viable),
	add_parse(GROUP_ID, group_id, "group_id"),
	add_parse(UINT32, het_job_offset, "hetjob_group"),
	add_parse(BOOL16, immediate, "immediate"),
	add_parse(UINT32, job_id, "job_id"),
	add_skip(job_id_str),
	add_parse(BOOL16, kill_on_node_fail, "kill_on_node_fail"),
	add_parse(STRING, licenses, "licenses"),
	add_skip(licenses_tot),
	add_flags(JOB_MAIL_FLAGS, mail_type, "mail_type"),
	add_parse(STRING, mail_user, "mail_user"),
	add_parse(STRING, mcs_label, "mcs_label"),
	add_parse(STRING, mem_bind, "memory_binding"),
	add_flags(MEMORY_BINDING_TYPE, mem_bind_type, "memory_binding_type"),
	add_parse(STRING, mem_per_tres, "memory_per_tres"),
	add_parse(STRING, name, "name"),
	add_parse(STRING, network, "network"),
	add_parse(NICE, nice, "nice"),
	add_parse(UINT32, num_tasks, "tasks"),
	add_flags(OPEN_MODE, open_mode, "open_mode"),
	add_skip(origin_cluster),
	add_parse(UINT16, other_port, "reserve_ports"),
	add_parse(BOOL, overcommit, "overcommit"),
	add_parse(STRING, partition, "partition"),
	add_parse(UINT16, plane_size, "distribution_plane_size"),
	add_flags(POWER_FLAGS, power_flags, "power_flags"),
	add_parse(STRING, prefer, "prefer"),
	add_parse(UINT32, priority, "priority"),
	add_parse(ACCT_GATHER_PROFILE, profile, "profile"),
	add_parse(STRING, qos, "qos"),
	add_parse(BOOL16, reboot, "reboot"),
	add_skip(resp_host),
	add_skip(restart_cnt),
	add_parse(CSV_LIST, req_nodes, "required_nodes"),
	add_parse(BOOL16, requeue, "requeue"),
	add_parse(STRING, reservation, "reservation"),
	add_parse(STRING, script, "script"),
	add_skip(script_buf),
	add_skip(script_hash),
	add_parse(JOB_SHARED, shared, "shared"),
	add_parse(UINT32, site_factor, "site_factor"),
	add_cparse(JOB_DESC_MSG_SPANK_ENV, "spank_environment"),
	add_skip(spank_job_env),
	add_skip(spank_job_env_size),
	add_skip(submit_line),
	add_parse(TASK_DISTRIBUTION, task_dist, "distribution"),
	add_parse(UINT32_NO_VAL, time_limit, "time_limit"),
	add_parse(UINT32_NO_VAL, time_min, "time_minimum"),
	add_parse(STRING, tres_bind, "tres_bind"),
	add_parse(STRING, tres_freq, "tres_freq"),
	add_parse(STRING, tres_per_job, "tres_per_job"),
	add_parse(STRING, tres_per_node, "tres_per_node"),
	add_parse(STRING, tres_per_socket, "tres_per_socket"),
	add_parse(STRING, tres_per_task, "tres_per_task"),
	add_parse(USER_ID, user_id, "user_id"),
	add_parse(BOOL16_NO_VAL, wait_all_nodes, "wait_all_nodes"),
	add_flags(WARN_FLAGS, warn_flags, "kill_warning_flags"),
	add_parse(SIGNAL, warn_signal, "kill_warning_signal"),
	add_parse(UINT16_NO_VAL, warn_time, "kill_warning_delay"),
	add_parse(STRING, work_dir, "current_working_directory"),
	add_parse(UINT16, cpus_per_task, "cpus_per_task"),
	add_parse(UINT32, min_cpus, "minimum_cpus"),
	add_parse(UINT32, max_cpus, "maximum_cpus"),
	add_cparse(JOB_DESC_MSG_NODES, "nodes"),
	add_parse(UINT32, min_nodes, "minimum_nodes"),
	add_parse(UINT32, max_nodes, "maximum_nodes"),
	add_parse(UINT16, boards_per_node, "minimum_boards_per_node"),
	add_parse(UINT16, sockets_per_board, "minimum_sockets_per_board"),
	add_parse(UINT16, sockets_per_node, "sockets_per_node"),
	add_parse(UINT16, threads_per_core, "threads_per_core"),
	add_parse(UINT16, ntasks_per_node, "tasks_per_node"),
	add_parse(UINT16, ntasks_per_socket, "tasks_per_socket"),
	add_parse(UINT16, ntasks_per_core, "tasks_per_core"),
	add_parse(UINT16, ntasks_per_board, "tasks_per_board"),
	add_parse(UINT16, ntasks_per_tres, "ntasks_per_tres"),
	add_parse(UINT16, pn_min_cpus, "minimum_cpus_per_node"),
	add_parse_overload(JOB_MEM_PER_CPU, pn_min_memory, 1, "memory_per_cpu"),
	add_parse_overload(JOB_MEM_PER_NODE, pn_min_memory, 1, "memory_per_node"),
	add_parse(UINT32, pn_min_tmp_disk, "temporary_disk_per_node"),
	add_parse(STRING, req_context, "selinux_context"),
	add_parse(UINT32_NO_VAL, req_switch, "required_switches"),
	add_skip(select_jobinfo),
	add_skip(selinux_context),
	add_parse(STRING, std_err, "standard_error"),
	add_parse(STRING, std_in, "standard_input"),
	add_parse(STRING, std_out, "standard_output"),
	add_skip(tres_req_cnt),
	add_parse(UINT32, wait4switch, "wait_for_switch"),
	add_parse(STRING, wckey, "wckey"),
	add_flags(X11_FLAGS, x11, "x11"),
	add_parse(STRING, x11_magic_cookie, "x11_magic_cookie"),
	add_parse(STRING, x11_target, "x11_target_host"),
	add_parse(UINT16, x11_target_port, "x11_target_port"),
};
#undef add_parse
#undef add_parse_overload
#undef add_cparse
#undef add_skip
#undef add_flags

#undef add_parser
#undef add_parser_skip
#undef add_complex_parser
#undef add_parse_bool

/* add parser array (for struct) */
#define addpa(typev, typet)                                                    \
	{                                                                      \
		.magic = MAGIC_PARSER,                                         \
		.model = PARSER_MODEL_ARRAY,                                   \
		.type = DATA_PARSER_##typev,                                   \
		.type_string = XSTRINGIFY(DATA_PARSER_ ## typev),              \
		.obj_type_string = XSTRINGIFY(typet),                          \
		.size = sizeof(typet),                                         \
		.needs = NEED_NONE,                                            \
		.fields = PARSER_ARRAY(typev),                                 \
		.field_count = ARRAY_SIZE(PARSER_ARRAY(typev)),                \
		.flag = FLAG_TYPE_NONE,                                        \
		.flag_bit_array_count = NO_VAL8,                               \
		.ptr_offset = NO_VAL,                                          \
	}
/* add parser for List */
#define addpl(typev, typel, delf, addf, need)                                  \
	{                                                                      \
		.magic = MAGIC_PARSER,                                         \
		.model = PARSER_MODEL_LIST,                                    \
		.type = DATA_PARSER_##typev,                                   \
		.type_string = XSTRINGIFY(DATA_PARSER_ ## typev),              \
		.obj_type_string = XSTRINGIFY(List),                           \
		.list_type = DATA_PARSER_##typel,                              \
		.list_del_func = delf,                                         \
		.list_new_func = addf,                                         \
		.size = sizeof(List),                                          \
		.needs = need,                                                 \
		.flag = FLAG_TYPE_NONE,                                        \
		.flag_bit_array_count = NO_VAL8,                               \
		.ptr_offset = NO_VAL,                                          \
	}
/* add parser for simple type */
#define addps(typev, stype, need)                                              \
	{                                                                      \
		.magic = MAGIC_PARSER,                                         \
		.type = DATA_PARSER_##typev,                                   \
		.model = PARSER_MODEL_SIMPLE,                                  \
		.type_string = XSTRINGIFY(DATA_PARSER_ ## typev),              \
		.obj_type_string = XSTRINGIFY(stype),                          \
		.size = sizeof(stype),                                         \
		.needs = need,                                                 \
		.parse = PARSE_FUNC(typev),                                    \
		.dump = DUMP_FUNC(typev),                                      \
		.flag = FLAG_TYPE_NONE,                                        \
		.flag_bit_array_count = NO_VAL8,                               \
		.ptr_offset = NO_VAL,                                          \
	}
/* add parser for complex type */
#define addpc(typev, typet, need)                                              \
	{                                                                      \
		.magic = MAGIC_PARSER,                                         \
		.type = DATA_PARSER_##typev,                                   \
		.model = PARSER_MODEL_COMPLEX,                                 \
		.type_string = XSTRINGIFY(DATA_PARSER_ ## typev),              \
		.obj_type_string = XSTRINGIFY(typet),                          \
		.size = sizeof(typet),                                         \
		.needs = need,                                                 \
		.parse = PARSE_FUNC(typev),                                    \
		.dump = DUMP_FUNC(typev),                                      \
		.flag = FLAG_TYPE_NONE,                                        \
		.flag_bit_array_count = NO_VAL8,                               \
		.ptr_offset = NO_VAL,                                          \
	}
#define addfa(typev, typet)                                                    \
	{                                                                      \
		.magic = MAGIC_PARSER,                                         \
		.model = PARSER_MODEL_FLAG_ARRAY,                              \
		.type = DATA_PARSER_##typev,                                   \
		.type_string = XSTRINGIFY(DATA_PARSER_ ## typev),              \
		.obj_type_string = XSTRINGIFY(typet),                          \
		.size = sizeof(typet),                                         \
		.needs = NEED_NONE,                                            \
		.flag = FLAG_TYPE_BIT_ARRAY,                                   \
		.flag_bit_array = PARSER_FLAG_ARRAY(typev),                    \
		.flag_bit_array_count = ARRAY_SIZE(PARSER_FLAG_ARRAY(typev)),  \
		.ptr_offset = NO_VAL,                                          \
	}
static const parser_t parsers[] = {
	/* Simple type parsers */
	addps(STRING, char *, NEED_NONE),
	addps(UINT32, uint32_t, NEED_NONE),
	addps(UINT32_NO_VAL, uint32_t, NEED_NONE),
	addps(UINT64, uint64_t, NEED_NONE),
	addps(UINT16, uint16_t, NEED_NONE),
	addps(UINT16_NO_VAL, uint16_t, NEED_NONE),
	addps(INT64, int64_t, NEED_NONE),
	addps(FLOAT128, long double, NEED_NONE),
	addps(FLOAT64, double, NEED_NONE),
	addps(FLOAT64_NO_VAL, double, NEED_NONE),
	addps(BOOL, uint8_t, NEED_NONE),
	addps(BOOL16, uint16_t, NEED_NONE),
	addps(BOOL16_NO_VAL, uint16_t, NEED_NONE),
	addps(QOS_NAME, char *, NEED_QOS),
	addps(QOS_ID, uint32_t, NEED_NONE),
	addps(QOS_STRING_ID_LIST, List, NEED_NONE),
	addps(JOB_EXIT_CODE, int32_t, NEED_NONE),
	addps(ADMIN_LVL, uint16_t, NEED_NONE),
	addps(ASSOC_ID, uint32_t, NEED_ASSOC),
	addps(STATS_REC_ARRAY, slurmdb_stats_rec_t, NEED_NONE),
	addps(STATS_REC_ARRAY_PTR, slurmdb_stats_rec_t *, NEED_NONE),
	addps(RPC_ID, slurmdbd_msg_type_t, NEED_NONE),
	addps(SELECT_PLUGIN_ID, int, NEED_NONE),
	addps(TASK_DISTRIBUTION, uint32_t, NEED_NONE),
	addps(SLURM_STEP_ID, slurm_step_id_t, NEED_NONE),
	addps(STEP_ID, uint32_t, NEED_NONE),
	addps(WCKEY_TAG, char *, NEED_NONE),
	addps(GROUP_ID, gid_t, NEED_NONE),
	addps(JOB_REASON, uint32_t, NEED_NONE),
	addps(JOB_STATE, uint32_t, NEED_NONE),
	addps(USER_ID, uid_t, NEED_NONE),
	addps(TRES_STR, char *, NEED_TRES),
	addps(ASSOC_SHORT_PTR, slurmdb_assoc_rec_t *, NEED_NONE),
	addps(ASSOC_USAGE_PTR, slurmdb_assoc_usage_t *, NEED_NONE),
	addps(CSV_LIST, char *, NEED_NONE),
	addps(LICENSES, license_info_msg_t, NEED_NONE),
	addps(CORE_SPEC, uint16_t, NEED_NONE),
	addps(THREAD_SPEC, uint16_t, NEED_NONE),
	addps(NICE, uint32_t, NEED_NONE),
	addps(JOB_MEM_PER_CPU, uint64_t, NEED_NONE),
	addps(JOB_MEM_PER_NODE, uint64_t, NEED_NONE),
	addps(ACCT_GATHER_PROFILE, uint32_t, NEED_NONE),
	addps(JOB_SHARED, uint16_t, NEED_NONE),
	addps(ALLOCATED_CORES, uint32_t, NEED_NONE),
	addps(ALLOCATED_CPUS, uint32_t, NEED_NONE),
	addps(JOB_RES_PTR, job_resources_t *, NEED_NONE),
	addps(CONTROLLER_PING_MODE, int, NEED_NONE),
	addps(CONTROLLER_PING_RESULT, bool, NEED_NONE),
	addps(CONTROLLER_PING_ARRAY, controller_ping_t *, NEED_NONE),
	addps(HOSTLIST, hostlist_t, NEED_NONE),
	addps(CPU_FREQ_FLAGS, uint32_t, NEED_NONE),
	addps(PARTITION_INFO_PTR, partition_info_t *, NEED_NONE),
	addps(NODE_ARRAY, node_info_t **, NEED_NONE),
	addps(PARTITION_INFO_ARRAY, partition_info_t **, NEED_NONE),
	addps(STEP_INFO_ARRAY, job_step_info_t **, NEED_NONE),
	addps(ACCT_GATHER_ENERGY_PTR, acct_gather_energy_t *, NEED_NONE),
	addps(EXT_SENSORS_DATA_PTR, ext_sensors_data_t *, NEED_NONE),
	addps(POWER_MGMT_DATA_PTR, power_mgmt_data_t *, NEED_NONE),
	addps(NODE_STATES_NO_VAL, uint32_t, NEED_NONE),
	addps(RESERVATION_INFO_ARRAY, reserve_info_t **, NEED_NONE),
	addps(ERROR, int, NEED_NONE),
	addps(JOB_INFO_MSG, job_info_msg_t, NEED_NONE),
	addps(JOB_DESC_MSG_PTR, job_desc_msg_t *, NEED_NONE),
	addps(STRING_ARRAY, char **, NEED_NONE),
	addps(SIGNAL, uint16_t, NEED_NONE),
	addps(CRON_ENTRY_PTR, cron_entry_t *, NEED_NONE),
	addps(BITSTR, bitstr_t, NEED_NONE),

	/* Complex type parsers */
	addpc(QOS_PREEMPT_LIST, slurmdb_qos_rec_t, NEED_QOS),
	addpc(STEP_NODES, slurmdb_step_rec_t, NEED_TRES),
	addpc(STEP_TRES_REQ_MAX, slurmdb_step_rec_t, NEED_TRES),
	addpc(STEP_TRES_REQ_MIN, slurmdb_step_rec_t, NEED_TRES),
	addpc(STEP_TRES_USAGE_MAX, slurmdb_step_rec_t, NEED_TRES),
	addpc(STEP_TRES_USAGE_MIN, slurmdb_step_rec_t, NEED_TRES),
	addpc(STATS_MSG_CYCLE_MEAN, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_CYCLE_MEAN_DEPTH, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_CYCLE_PER_MIN, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_BF_CYCLE_MEAN, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_BF_DEPTH_MEAN, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_BF_DEPTH_MEAN_TRY, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_BF_QUEUE_LEN_MEAN, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_BF_TABLE_SIZE_MEAN, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_BF_ACTIVE, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_RPCS_BY_TYPE, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_RPCS_BY_USER, stats_info_response_msg_t, NEED_NONE),
	addpc(NODE_SELECT_ALLOC_MEMORY, node_info_t, NEED_NONE),
	addpc(NODE_SELECT_ALLOC_CPUS, node_info_t, NEED_NONE),
	addpc(NODE_SELECT_ALLOC_IDLE_CPUS, node_info_t, NEED_NONE),
	addpc(NODE_SELECT_TRES_USED, node_info_t, NEED_NONE),
	addpc(NODE_SELECT_TRES_WEIGHTED, node_info_t, NEED_NONE),
	addpc(NODES, node_info_msg_t, NEED_NONE),
	addpc(JOB_INFO_GRES_DETAIL, slurm_job_info_t, NEED_NONE),
	addpc(JOB_RES_NODES, job_resources_t, NEED_NONE),
	addpc(STEP_INFO_MSG, job_step_info_response_msg_t *, NEED_TRES),
	addpc(PARTITION_INFO_MSG, partition_info_msg_t, NEED_TRES),
	addpc(RESERVATION_INFO_MSG, reserve_info_msg_t, NEED_NONE),
	addpc(RESERVATION_INFO_CORE_SPEC, reserve_info_t, NEED_NONE),
	addpc(JOB_ARRAY_RESPONSE_MSG, job_array_resp_msg_t, NEED_NONE),
	addpc(JOB_DESC_MSG_ARGV, job_desc_msg_t, NEED_NONE),
	addpc(JOB_DESC_MSG_CPU_FREQ, job_desc_msg_t, NEED_NONE),
	addpc(JOB_DESC_MSG_ENV, job_desc_msg_t, NEED_NONE),
	addpc(JOB_DESC_MSG_SPANK_ENV, job_desc_msg_t, NEED_NONE),
	addpc(JOB_DESC_MSG_NODES, job_desc_msg_t, NEED_NONE),
	addpc(JOB_INFO_STDIN, slurm_job_info_t, NEED_NONE),
	addpc(JOB_INFO_STDOUT, slurm_job_info_t, NEED_NONE),
	addpc(JOB_INFO_STDERR, slurm_job_info_t, NEED_NONE),
	addpc(JOB_USER, slurmdb_job_rec_t, NEED_NONE),

	/* Array of parsers */
	addpa(ASSOC_SHORT, slurmdb_assoc_rec_t),
	addpa(ASSOC, slurmdb_assoc_rec_t),
	addpa(USER, slurmdb_user_rec_t),
	addpa(JOB, slurmdb_job_rec_t),
	addpa(STEP, slurmdb_step_rec_t),
	addpa(ACCOUNT, slurmdb_account_rec_t),
	addpa(ACCOUNTING, slurmdb_accounting_rec_t),
	addpa(COORD, slurmdb_coord_rec_t),
	addpa(WCKEY, slurmdb_wckey_rec_t),
	addpa(TRES, slurmdb_tres_rec_t),
	addpa(TRES_NCT, slurmdb_tres_nct_rec_t),
	addpa(QOS, slurmdb_qos_rec_t),
	addpa(STATS_REC, slurmdb_stats_rec_t),
	addpa(CLUSTER_REC, slurmdb_cluster_rec_t),
	addpa(CLUSTER_ACCT_REC, slurmdb_cluster_accounting_rec_t),
	addpa(ASSOC_USAGE, slurmdb_assoc_usage_t),
	addpa(STATS_RPC, slurmdb_rpc_obj_t),
	addpa(STATS_USER, slurmdb_rpc_obj_t),
	addpa(STATS_MSG, stats_info_response_msg_t),
	addpa(NODE, node_info_t),
	addpa(LICENSE, slurm_license_info_t),
	addpa(JOB_INFO, slurm_job_info_t),
	addpa(JOB_RES, job_resources_t),
	addpa(CONTROLLER_PING, controller_ping_t),
	addpa(STEP_INFO, job_step_info_t),
	addpa(PARTITION_INFO, partition_info_t),
	addpa(SINFO_DATA, sinfo_data_t),
	addpa(ACCT_GATHER_ENERGY, acct_gather_energy_t),
	addpa(EXT_SENSORS_DATA, ext_sensors_data_t),
	addpa(POWER_MGMT_DATA, power_mgmt_data_t),
	addpa(RESERVATION_INFO, reserve_info_t),
	addpa(RESERVATION_CORE_SPEC, resv_core_spec_t),
	addpa(JOB_SUBMIT_RESPONSE_MSG, submit_response_msg_t),
	addpa(JOB_DESC_MSG, job_desc_msg_t),
	addpa(CRON_ENTRY, cron_entry_t),

	/* Flag bit arrays */
	addfa(ASSOC_FLAGS, uint16_t),
	addfa(USER_FLAGS, uint32_t),
	addfa(SLURMDB_JOB_FLAGS, uint32_t),
	addfa(ACCOUNT_FLAGS, uint32_t),
	addfa(WCKEY_FLAGS, uint32_t),
	addfa(QOS_FLAGS, uint32_t),
	addfa(QOS_PREEMPT_MODES, uint16_t),
	addfa(CLUSTER_REC_FLAGS, uint32_t),
	addfa(NODE_STATES, uint32_t),
	addfa(JOB_FLAGS, uint64_t),
	addfa(JOB_SHOW_FLAGS, uint16_t),
	addfa(POWER_FLAGS, uint8_t),
	addfa(JOB_MAIL_FLAGS, uint16_t),
	addfa(RESERVATION_FLAGS, uint64_t),
	addfa(CPU_BINDING_FLAGS, uint16_t), /* cpu_bind_type_t */
	addfa(CRON_ENTRY_FLAGS, uint32_t),
	addfa(MEMORY_BINDING_TYPE, uint16_t), /* mem_bind_type_t */
	addfa(WARN_FLAGS, uint16_t),
	addfa(X11_FLAGS, uint16_t),
	addfa(OPEN_MODE, uint8_t),

	/* List parsers */
	addpl(QOS_LIST, QOS, slurmdb_destroy_qos_rec, create_qos_rec_obj, NEED_QOS),
	addpl(QOS_NAME_LIST, QOS_NAME, xfree_ptr, create_parser_list_obj, NEED_QOS),
	addpl(QOS_ID_LIST, QOS_ID, xfree_ptr, create_parser_list_obj, NEED_QOS),
	addpl(QOS_STRING_ID_LIST, QOS_STRING_ID, xfree_ptr, create_qos_rec_obj, NEED_QOS),
	addpl(USER_LIST, USER, slurmdb_destroy_user_rec, create_user_rec_obj, NEED_NONE),
	addpl(WCKEY_LIST, WCKEY, slurmdb_destroy_wckey_rec, create_wckey_rec_obj, NEED_NONE),
	addpl(ACCOUNT_LIST, ACCOUNT, slurmdb_destroy_account_rec, create_parser_list_obj, NEED_NONE),
	addpl(ACCOUNTING_LIST, ACCOUNTING, slurmdb_destroy_accounting_rec, create_parser_list_obj, NEED_NONE),
	addpl(CLUSTER_REC_LIST, CLUSTER_REC, slurmdb_destroy_cluster_rec, create_cluster_rec_obj, NEED_NONE),
	addpl(ASSOC_LIST, ASSOC, slurmdb_destroy_assoc_rec, create_assoc_rec_obj, NEED_NONE),
	addpl(ASSOC_SHORT_LIST, ASSOC_SHORT, slurmdb_destroy_assoc_rec, create_assoc_rec_obj, NEED_NONE),
	addpl(COORD_LIST, COORD, slurmdb_destroy_coord_rec, create_parser_list_obj, NEED_NONE),
	addpl(CLUSTER_ACCT_REC_LIST, CLUSTER_ACCT_REC, slurmdb_destroy_clus_res_rec, create_parser_list_obj, NEED_NONE),
	addpl(JOB_LIST, JOB, slurmdb_destroy_job_rec, create_job_rec_obj, NEED_NONE),
	addpl(STEP_LIST, STEP, slurmdb_destroy_step_rec, create_step_rec_obj, NEED_NONE),
	addpl(STATS_RPC_LIST, STATS_RPC, NULL, NULL, NEED_NONE),
	addpl(STATS_USER_LIST, STATS_USER, NULL, NULL, NEED_NONE),
	addpl(TRES_LIST, TRES, slurmdb_destroy_tres_rec, create_parser_list_obj, NEED_NONE),
	addpl(SINFO_DATA_LIST, SINFO_DATA, NULL, NULL, NEED_NONE),
	addpl(JOB_DESC_MSG_LIST, JOB_DESC_MSG, (ListDelF) slurm_free_job_desc_msg, create_job_desc_msg_obj, NEED_NONE),
};
#undef addpl
#undef addps
#undef addpc
#undef addpa

// clang-format on

extern const parser_t *const find_parser_by_type(type_t type)
{
	for (int i = 0; i < ARRAY_SIZE(parsers); i++)
		if (parsers[i].type == type)
			return &parsers[i];

	fatal_abort("%s: failed to find parser with type %u", __func__, type);
}

extern void parsers_init()
{
#ifndef NDEBUG
	/* sanity check the parsers */
	for (int i = 0; i < ARRAY_SIZE(parsers); i++)
		check_parser(&parsers[i]);
#endif /* !NDEBUG */
}

#ifndef NDEBUG

extern void verify_parser_not_sliced_funcname(const parser_t *const parser,
					      const char *func,
					      const char *file, int line)
{
	for (int i = 0; i < ARRAY_SIZE(parsers); i++) {
		if (parsers[i].fields) {
			const parser_t *const fparser = parsers[i].fields;
			for (int j = 0; j < parsers[i].field_count; j++)
				if (&fparser[j] == parser)
					fatal_abort("%s: direct reference of linking parser %s(0x%" PRIxPTR ") inside of parser array %s(0x%" PRIxPTR ")[%d]=%s(0x%" PRIxPTR ") detected as %s:%d",
						func, parser->type_string,
						(uintptr_t) parser,
						fparser->type_string,
						(uintptr_t) fparser, j,
						fparser[j].type_string,
						(uintptr_t) &fparser[j], file,
						line);
		}
	}
}

extern void verify_parser_sliced_funcname(const parser_t *const parser,
					  const char *func, const char *file,
					  int line)
{
	for (int i = 0; i < ARRAY_SIZE(parsers); i++) {
		if (&parsers[i] == parser) {
			fatal_abort("%s: expected linking parser %s(0x%" PRIxPTR ") inside of parser array %s:%d",
				    func, parser->type_string,
				    (uintptr_t) parser, file, line);
		}

		if (parsers[i].fields) {
			const parser_t *const fparser = parsers[i].fields;
			for (int j = 0; j < parsers[i].field_count; j++)
				if (&fparser[j] == parser)
					return;
		}
	}

	fatal_abort("%s: orphan parser %s(0x%" PRIxPTR ") detected", func,
		    parser->type_string, (uintptr_t) parser);
}

#endif /* !NDEBUG */
